#include "c6_network.h"
#include "c6_net_helpers.h"
#include "c6_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs.h"

static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static c6_network_status_t s_status;
static c6_net_store_t s_profiles={.version=1}; /* display-owner only */
static char s_device_id[65],s_name[32],s_hostname[32];
static bool s_initialized,s_wifi_initialized,s_events_registered,s_mdns;
static esp_netif_t *s_sta,*s_ap;
static httpd_handle_t s_http;
static size_t s_http_routes;
static bool s_http_post;
static QueueHandle_t s_retry_queue;
static bool s_retry_pending;
static bool s_skip_leave; /* guarded with status lock */
static int64_t s_join_deadline; /* owner only */
static unsigned s_attempt; /* owner only; bounded across all saved profiles */
static esp_err_t start_ap(void);
static esp_err_t start_http(void);
static esp_err_t connect_saved(void);

static void error_set(const char *text) {
    portENTER_CRITICAL(&s_lock);snprintf(s_status.last_error,sizeof(s_status.last_error),"%s",text?text:"");portEXIT_CRITICAL(&s_lock);
}
void c6_network_get_status(c6_network_status_t *out) {
    if(!out)return;
    portENTER_CRITICAL(&s_lock);*out=s_status;portEXIT_CRITICAL(&s_lock);
}
void c6_network_ble_status(bool started,bool advertising,bool connected,const char *error) {
    portENTER_CRITICAL(&s_lock);
    s_status.ble_started=started;s_status.ble_advertising=advertising;s_status.ble_connected=connected;
    if(error)snprintf(s_status.last_error,sizeof(s_status.last_error),"%s",error);
    portEXIT_CRITICAL(&s_lock);
}
static bool persist(const void *blob,size_t n,void *ctx) {
    (void)ctx;nvs_handle_t h;
    if(nvs_open("taby_c6_net",NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t e=nvs_set_blob(h,"profiles",blob,n);
    if(e==ESP_OK)e=nvs_commit(h);
    nvs_close(h);return e==ESP_OK;
}
static bool save(const c6_net_store_t *next) { return c6_net_save(&s_profiles,next,persist,NULL); }
static bool reply(char *out,size_t cap,const char *s) {
    if(!out||!cap)return false;
    if(strlen(s)>=cap){snprintf(out,cap,"TABY:ERR response_too_large");return false;}
    strcpy(out,s);return true;
}
static void json_reply(char *out,size_t cap,const char *prefix,cJSON *root) {
    char *body=root?cJSON_PrintUnformatted(root):NULL;
    if(!body)reply(out,cap,"TABY:ERR encode_failed");
    else if(strlen(prefix)+strlen(body)>=cap)reply(out,cap,"TABY:ERR response_too_large");
    else snprintf(out,cap,"%s%s",prefix,body);
    cJSON_free(body);cJSON_Delete(root);
}
static cJSON *network_array(void) {
    cJSON *a=cJSON_CreateArray();
    for(size_t i=0;i<s_profiles.count;++i){
        const c6_net_profile_t *p=&s_profiles.profiles[i];cJSON *o=cJSON_CreateObject();
        cJSON_AddStringToObject(o,"ssid",p->ssid);cJSON_AddStringToObject(o,"label",p->ssid);
        cJSON_AddNumberToObject(o,"priority",p->priority);cJSON_AddBoolToObject(o,"auto_join",true);
        cJSON_AddBoolToObject(o,"preferred",p->preferred);cJSON_AddBoolToObject(o,"last_success",p->last_success);
        cJSON_AddItemToArray(a,o);
    }
    return a;
}
static void setup_info(char *out,size_t cap) {
    c6_network_status_t s;c6_network_get_status(&s);cJSON *o=cJSON_CreateObject(),*ap=cJSON_CreateObject();
    cJSON_AddStringToObject(o,"device_id",s_device_id);
    cJSON_AddStringToObject(o,"wifi_mode",s.ap_active?"setup_ap":s.wifi_started?"station":"none");
    cJSON_AddBoolToObject(o,"provisioned",s_profiles.count>0);cJSON_AddBoolToObject(o,"connected",s.wifi_connected);
    cJSON_AddStringToObject(o,"wifi_error",s.last_error);
    cJSON_AddStringToObject(ap,"ssid",s.ap_ssid);cJSON_AddStringToObject(ap,"password",s.ap_password);
    cJSON_AddStringToObject(ap,"host",s.ap_active?"192.168.4.1":"");
    /* Wi-Fi QR escaping is separate from JSON escaping. */
    char qr[160],escaped[70];size_t n=0;
    for(size_t i=0;s.ap_ssid[i];++i){if(strchr("\\;,:\"",s.ap_ssid[i]))escaped[n++]='\\';escaped[n++]=s.ap_ssid[i];}escaped[n]=0;
    snprintf(qr,sizeof(qr),"WIFI:T:WPA;S:%s;P:%s;;",escaped,s.ap_password);
    cJSON_AddStringToObject(ap,"qr_payload",s.ap_active?qr:"");
    cJSON_AddItemToObject(o,"setup_ap",ap);cJSON_AddItemToObject(o,"saved_networks",network_array());
    json_reply(out,cap,"TABY:SETUP ",o);
}

/* Dedicated worker keeps waits out of the ESP event loop and timer service. */
static void retry_task(void *arg) {
    (void)arg;uint8_t msg=1;unsigned delay=1;char response[96];
    for(;;){
        bool queued=xQueueReceive(s_retry_queue,&msg,pdMS_TO_TICKS(5000))==pdTRUE;
        if(queued)vTaskDelay(pdMS_TO_TICKS(delay*1000));
        if(!c6_runtime_request("C6_NET_RETRY",response,sizeof(response),2000)) {
            xQueueSend(s_retry_queue,&msg,0);
        }
        if(queued)delay=delay<8?delay*2:8;
        c6_network_status_t s;c6_network_get_status(&s);if(s.wifi_connected)delay=1;
    }
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){
        const wifi_event_sta_disconnected_t *event=data;
        portENTER_CRITICAL(&s_lock);
        bool deliberate=s_skip_leave && event->reason==WIFI_REASON_ASSOC_LEAVE;
        s_skip_leave=false;
        if(deliberate){portEXIT_CRITICAL(&s_lock);return;}
        s_retry_pending=true;s_status.wifi_connected=false;s_status.ip[0]=0;
        snprintf(s_status.last_error,sizeof(s_status.last_error),"wifi_disconnected");portEXIT_CRITICAL(&s_lock);
        uint8_t msg=1;xQueueSend(s_retry_queue,&msg,0);
    } else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ip=data;
        portENTER_CRITICAL(&s_lock);s_skip_leave=false;s_retry_pending=false;s_status.wifi_connected=true;
        snprintf(s_status.ip,sizeof(s_status.ip),IPSTR,IP2STR(&ip->ip_info.ip));s_status.last_error[0]=0;portEXIT_CRITICAL(&s_lock);
        /* Persist success on owner via the same bounded queue. */
        uint8_t msg=1;xQueueSend(s_retry_queue,&msg,0);
    }
}
static esp_err_t ensure_wifi(void) {
    if(!s_initialized)return ESP_ERR_INVALID_STATE;
    if(!s_retry_queue) {
        s_retry_queue=xQueueCreate(2,sizeof(uint8_t));if(!s_retry_queue)return ESP_ERR_NO_MEM;
        if(xTaskCreate(retry_task,"c6_wifi_retry",4096,NULL,3,NULL)!=pdPASS){vQueueDelete(s_retry_queue);s_retry_queue=NULL;return ESP_ERR_NO_MEM;}
    }
    esp_err_t e=esp_netif_init();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return e;
    e=esp_event_loop_create_default();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return e;
    if(!s_sta)s_sta=esp_netif_create_default_wifi_sta();
    if(!s_ap)s_ap=esp_netif_create_default_wifi_ap();
    if(!s_sta||!s_ap)return ESP_ERR_NO_MEM;
    e=esp_netif_set_hostname(s_sta,s_hostname);if(e!=ESP_OK)return e;
    if(!s_wifi_initialized){wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();e=esp_wifi_init(&cfg);if(e!=ESP_OK)return e;s_wifi_initialized=true;}
    if(!s_events_registered) {
        e=esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,wifi_event,NULL);if(e!=ESP_OK)return e;
        e=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL);
        if(e!=ESP_OK){esp_event_handler_unregister(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,wifi_event);return e;}
        s_events_registered=true;
    }
    c6_network_status_t s;c6_network_get_status(&s);
    if(!s.wifi_started){
        e=esp_wifi_set_storage(WIFI_STORAGE_RAM);if(e!=ESP_OK)return e;
        /* Configure a WPA2 AP while radios are stopped. Later APSTA activation
           can never briefly expose the driver's default/open AP settings. */
        wifi_config_t ap_cfg={0};
        memcpy(ap_cfg.ap.ssid,s_name,strlen(s_name));
        static const char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
        for(unsigned i=0;i<12;++i)ap_cfg.ap.password[i]=alphabet[esp_random()%(sizeof(alphabet)-1)];
        ap_cfg.ap.authmode=WIFI_AUTH_WPA2_PSK;ap_cfg.ap.max_connection=2;
        ap_cfg.ap.channel=1;ap_cfg.ap.ssid_len=strlen(s_name);
        e=esp_wifi_set_mode(WIFI_MODE_APSTA);if(e!=ESP_OK)return e;
        e=esp_wifi_set_config(WIFI_IF_AP,&ap_cfg);if(e!=ESP_OK){memset(&ap_cfg,0,sizeof(ap_cfg));return e;}
        portENTER_CRITICAL(&s_lock);
        snprintf(s_status.ap_ssid,sizeof(s_status.ap_ssid),"%s",s_name);
        memcpy(s_status.ap_password,ap_cfg.ap.password,12);s_status.ap_password[12]=0;
        portEXIT_CRITICAL(&s_lock);memset(&ap_cfg,0,sizeof(ap_cfg));
        e=esp_wifi_set_mode(WIFI_MODE_STA);if(e!=ESP_OK)return e;
        e=esp_wifi_start();if(e!=ESP_OK)return e;
        portENTER_CRITICAL(&s_lock);s_status.wifi_started=true;portEXIT_CRITICAL(&s_lock);
    }
    e=start_http();if(e!=ESP_OK)return e;
    if(!s_mdns && mdns_init()==ESP_OK) {
        if(mdns_hostname_set(s_hostname)==ESP_OK && mdns_instance_name_set(s_name)==ESP_OK && mdns_service_add(NULL,"_http","_tcp",80,NULL,0)==ESP_OK)s_mdns=true;
        else mdns_free();
    }
    return ESP_OK;
}
static esp_err_t start_ap(void) {
    esp_err_t e=ensure_wifi();if(e!=ESP_OK)return e;
    c6_network_status_t s;c6_network_get_status(&s);if(s.ap_active)return ESP_OK;
    e=esp_wifi_set_mode(WIFI_MODE_APSTA);if(e!=ESP_OK)return e;
    portENTER_CRITICAL(&s_lock);s_status.ap_active=true;portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}
static esp_err_t connect_saved(void) {
    esp_err_t e=ensure_wifi();if(e!=ESP_OK)return e;
    if(!s_profiles.count)return start_ap();
    if(s_attempt>=s_profiles.count*3){error_set("wifi_retries_exhausted");return start_ap();}
    unsigned order[C6_NET_MAX_PROFILES];for(unsigned i=0;i<s_profiles.count;++i)order[i]=i;
    for(unsigned i=0;i<s_profiles.count;++i)for(unsigned j=i+1;j<s_profiles.count;++j){
        const c6_net_profile_t *a=&s_profiles.profiles[order[i]],*b=&s_profiles.profiles[order[j]];
        if(b->preferred>a->preferred || (b->preferred==a->preferred&&b->priority>a->priority)){unsigned t=order[i];order[i]=order[j];order[j]=t;}
    }
    const c6_net_profile_t *p=&s_profiles.profiles[order[(s_attempt/3)%s_profiles.count]];++s_attempt;
    wifi_config_t cfg={0};memcpy(cfg.sta.ssid,p->ssid,strlen(p->ssid));memcpy(cfg.sta.password,p->password,strlen(p->password));
    cfg.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;cfg.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
    portENTER_CRITICAL(&s_lock);s_retry_pending=false;s_status.wifi_connected=false;s_status.ip[0]=0;
    snprintf(s_status.station_ssid,sizeof(s_status.station_ssid),"%s",p->ssid);portEXIT_CRITICAL(&s_lock);
    e=esp_wifi_set_config(WIFI_IF_STA,&cfg);memset(cfg.sta.password,0,sizeof(cfg.sta.password));
    if(e==ESP_OK)e=esp_wifi_connect();
    s_join_deadline=e==ESP_OK?esp_timer_get_time()+30000000LL:0;
    if(e!=ESP_OK){
        portENTER_CRITICAL(&s_lock);s_retry_pending=true;portEXIT_CRITICAL(&s_lock);
        error_set("wifi_connect_failed");uint8_t msg=1;xQueueSend(s_retry_queue,&msg,0);
    }
    return e;
}

/* Explicit reconfiguration cancels an old join, but its ASSOC_LEAVE callback
   must not immediately tear down the replacement join. Automatic retries
   already run after a disconnection and never call this function. */
static void disconnect_for_config(void) {
    s_join_deadline=0;
    portENTER_CRITICAL(&s_lock);s_skip_leave=true;s_status.wifi_connected=false;s_status.ip[0]=0;portEXIT_CRITICAL(&s_lock);
    if(esp_wifi_disconnect()!=ESP_OK){portENTER_CRITICAL(&s_lock);s_skip_leave=false;portEXIT_CRITICAL(&s_lock);}
}
esp_err_t c6_network_init(const char *device_id) {
    if(s_initialized)return ESP_OK;
    if(!device_id||strlen(device_id)>64)return ESP_ERR_INVALID_ARG;
    snprintf(s_device_id,sizeof(s_device_id),"%s",device_id);
    char suffix[9]={0};size_t n=strlen(device_id),j=0;
    for(size_t i=n>8?n-8:0;i<n;++i)if((device_id[i]>='0'&&device_id[i]<='9')||(device_id[i]>='a'&&device_id[i]<='z')||(device_id[i]>='A'&&device_id[i]<='Z'))suffix[j++]=device_id[i];
    if(!j)snprintf(suffix,sizeof(suffix),"C6");
    snprintf(s_name,sizeof(s_name),"Taby-%s",suffix);snprintf(s_hostname,sizeof(s_hostname),"taby-%s",suffix);
    nvs_handle_t h;esp_err_t e=nvs_open("taby_c6_net",NVS_READONLY,&h);
    if(e==ESP_OK){
        c6_net_store_t loaded;size_t size=sizeof(loaded);e=nvs_get_blob(h,"profiles",&loaded,&size);nvs_close(h);
        if(e==ESP_OK&&c6_net_store_valid(&loaded,size))s_profiles=loaded;
        else if(e!=ESP_ERR_NVS_NOT_FOUND)error_set("wifi_profiles_invalid");
        memset(&loaded,0,sizeof(loaded));
    }else if(e!=ESP_ERR_NVS_NOT_FOUND){error_set("wifi_profiles_unavailable");return e;}
    s_initialized=true;return ESP_OK;
}
esp_err_t c6_network_activate(uint8_t mode) {
    if(!s_initialized)return ESP_ERR_INVALID_STATE;
    esp_err_t e=ESP_OK;
    if(mode==1){c6_network_status_t s;c6_network_get_status(&s);if(!s.wifi_started || (!s.station_ssid[0]&&!s.ap_active)){s_attempt=0;e=connect_saved();}else e=ensure_wifi();}
    else if(mode==2)e=c6_ble_start(s_name);
    else if(mode!=0)e=ESP_ERR_INVALID_ARG;
    if(e!=ESP_OK)error_set("transport_start_failed");
    return e;
}

bool c6_network_command(const char *cmd,char *out,size_t cap) {
    if(!cmd||!out||!cap)return false;
    if(strcmp(cmd,"C6_NET_RETRY")==0){
        c6_network_status_t s;c6_network_get_status(&s);
        if(s.wifi_connected){
            s_attempt=0;s_join_deadline=0;c6_net_store_t next=s_profiles;bool changed=false;
            for(unsigned i=0;i<next.count;++i)if(strcmp(next.profiles[i].ssid,s.station_ssid)==0&&!next.profiles[i].last_success){next.profiles[i].last_success=1;changed=true;}
            if(changed&&!save(&next))error_set("wifi_success_save_failed");
        }else {
            if(s_join_deadline && esp_timer_get_time()>=s_join_deadline){
                s_join_deadline=0;disconnect_for_config();
                portENTER_CRITICAL(&s_lock);s_retry_pending=true;portEXIT_CRITICAL(&s_lock);
                error_set("wifi_join_timeout");
            }
            portENTER_CRITICAL(&s_lock);bool pending=s_retry_pending;portEXIT_CRITICAL(&s_lock);
            if(pending){
                if(s_attempt<s_profiles.count*3)connect_saved();
                else if(s.wifi_started){
                    portENTER_CRITICAL(&s_lock);s_retry_pending=false;portEXIT_CRITICAL(&s_lock);
                    error_set("wifi_retries_exhausted");start_ap();
                }
            }
        }
        reply(out,cap,"TABY:OK NET_RETRY");return true;
    }
    if(strcmp(cmd,"SETUP_START")==0 || strcmp(cmd,"SETUP_INFO")==0){
        if(strcmp(cmd,"SETUP_START")==0&&start_ap()!=ESP_OK)reply(out,cap,"TABY:ERR setup_start_failed");else setup_info(out,cap);return true;
    }
    if(strcmp(cmd,"WIFI_NETWORKS")==0){cJSON *o=cJSON_CreateObject();cJSON_AddItemToObject(o,"networks",network_array());json_reply(out,cap,"TABY:WIFI_NETWORKS ",o);return true;}
    if(strcmp(cmd,"WIFI_NEARBY")==0){
        if(ensure_wifi()!=ESP_OK){reply(out,cap,"TABY:ERR wifi_nearby_failed");return true;}
        wifi_scan_config_t cfg={0};esp_err_t e=esp_wifi_scan_start(&cfg,true);
        if(e!=ESP_OK){reply(out,cap,"TABY:ERR wifi_nearby_failed");return true;}
        wifi_ap_record_t records[16];uint16_t n=16;e=esp_wifi_scan_get_ap_records(&n,records);
        if(e!=ESP_OK){esp_wifi_clear_ap_list();reply(out,cap,"TABY:ERR wifi_nearby_failed");return true;}
        cJSON *o=cJSON_CreateObject(),*a=cJSON_CreateArray();
        for(unsigned i=0;i<n;++i){records[i].ssid[32]=0;cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"ssid",(char*)records[i].ssid);cJSON_AddNumberToObject(p,"rssi",records[i].rssi);cJSON_AddBoolToObject(p,"secure",records[i].authmode!=WIFI_AUTH_OPEN);cJSON_AddItemToArray(a,p);}
        cJSON_AddItemToObject(o,"networks",a);json_reply(out,cap,"TABY:WIFI_NEARBY ",o);return true;
    }
    bool forget=strncmp(cmd,"WIFI_FORGET ",12)==0,prefer=strncmp(cmd,"WIFI_PREFER ",12)==0;
    if(forget||prefer){
        c6_net_store_t next=s_profiles;const char *ssid=cmd+12;
        bool ok=forget?c6_net_forget(&next,ssid):c6_net_prefer(&next,ssid);
        if(ok)ok=save(&next);
        if(ok){
            c6_network_status_t s;c6_network_get_status(&s);
            if(s.wifi_started&&(prefer||strcmp(ssid,s.station_ssid)==0)){
                s_attempt=0;disconnect_for_config();
                if(next.count)connect_saved();else{
                    portENTER_CRITICAL(&s_lock);s_status.station_ssid[0]=0;portEXIT_CRITICAL(&s_lock);start_ap();
                }
            }
        }
        reply(out,cap,ok?(forget?"TABY:OK WIFI_FORGET":"TABY:OK WIFI_PREFER"):(forget?"TABY:ERR wifi_forget_failed":"TABY:ERR wifi_prefer_failed"));
        memset(&next,0,sizeof(next));return true;
    }
    if(strncmp(cmd,"PROVISION ",10)==0){
        const char *end=NULL;const char *json=cmd+10;
        /* cJSON represents strings as C strings: reject escaped NUL before parsing. */
        if(!c6_net_json_safe(json)){reply(out,cap,"TABY:ERR provision_invalid_json");return true;}
        cJSON *o=cJSON_ParseWithOpts(json,&end,true);
        if(!o){reply(out,cap,"TABY:ERR provision_invalid_json");return true;}
        const cJSON *ssid=cJSON_GetObjectItemCaseSensitive(o,"ssid"),*pw=cJSON_GetObjectItemCaseSensitive(o,"password");
        bool ok=cJSON_IsObject(o)&&cJSON_IsString(ssid)&&cJSON_IsString(pw)&&c6_net_credentials_valid(ssid->valuestring,pw->valuestring);
        c6_net_store_t next=s_profiles;
        if(ok)ok=c6_net_upsert(&next,ssid->valuestring,pw->valuestring)&&save(&next);
        cJSON_Delete(o);memset(&next,0,sizeof(next));
        reply(out,cap,ok?"TABY:OK PROVISION":"TABY:ERR provision_failed");
        if(ok){
            c6_network_status_t s;c6_network_get_status(&s);s_attempt=0;
            if(s.wifi_started)disconnect_for_config();
            if(connect_saved()!=ESP_OK)error_set("wifi_join_pending_failed");
        }return true;
    }
    return false;
}

static esp_err_t send_error(httpd_req_t *r,const char *status,const char *text) {httpd_resp_set_status(r,status);httpd_resp_set_type(r,"text/plain");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_sendstr(r,text);}
static esp_err_t runtime_http(httpd_req_t *r,const char *cmd,const char *prefix,bool json) {
    /* On stack of explicitly enlarged HTTP task; runtime owns its own job copy. */
    char response[4096];
    if(!c6_runtime_request(cmd,response,sizeof(response),10000))return send_error(r,"503 Service Unavailable","TABY:ERR runtime_unavailable");
    if(strncmp(response,"TABY:ERR ",9)==0)return send_error(r,"400 Bad Request",response);
    const char *body=response;
    if(prefix){
        if(strncmp(body,prefix,strlen(prefix))!=0)return send_error(r,"502 Bad Gateway","invalid_runtime_response");
        body+=strlen(prefix);
    }
    httpd_resp_set_type(r,json?"application/json":"text/plain");httpd_resp_set_hdr(r,"Cache-Control","no-store");
    return httpd_resp_sendstr(r,body);
}
static const char setup_page[]=
"<!doctype html><meta name=viewport content='width=device-width'><title>Taby Wi-Fi</title>"
"<style>body{font:18px system-ui;max-width:28em;margin:3em auto;padding:1em}input,button{display:block;font:inherit;margin:1em 0;padding:.5em;max-width:90%}</style>"
"<h1>Connect Taby</h1><form><label>Wi-Fi name<input id=s maxlength=32 required autocomplete=off></label><label>Password<input id=p type=password maxlength=64 autocomplete=new-password></label><button>Save network</button></form><p id=e role=status></p>"
"<script>document.querySelector('form').onsubmit=async ev=>{ev.preventDefault();let e=document.getElementById('e');e.textContent='Saving…';try{let r=await fetch('/provision',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:document.getElementById('s').value,password:document.getElementById('p').value})});e.textContent=r.ok?'Saved. Taby is attempting to connect.':await r.text()}catch(x){e.textContent='Connection interrupted. Check Taby and try again.'}};</script>";
static esp_err_t http_get(httpd_req_t *r) {
    if(strcmp(r->uri,"/setup")==0){httpd_resp_set_type(r,"text/html");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_sendstr(r,setup_page);}
    if(strcmp(r->uri,"/ping")==0)return httpd_resp_sendstr(r,"!");
    if(strcmp(r->uri,"/state")==0)return runtime_http(r,"STATE","TABY:STATE ",false);
    if(strcmp(r->uri,"/touch")==0)return runtime_http(r,"TOUCH","TABY:TOUCH ",false);
    if(strcmp(r->uri,"/v1/reusable/choice-signal")==0)return runtime_http(r,"CHOICE_SIGNAL","TABY:CHOICE_SIGNAL ",true);
    if(strcmp(r->uri,"/v1/health")==0)return runtime_http(r,"INFO","TABY:INFO ",true);
    if(strncmp(r->uri,"/cmd?",5)==0){
        size_t n=httpd_req_get_url_query_len(r);if(!n||n>=3072)return send_error(r,"400 Bad Request","invalid_query");
        char *query=malloc(n+1),*command=malloc(n+1);if(!query||!command){free(query);free(command);return send_error(r,"503 Service Unavailable","no_memory");}
        bool ok=httpd_req_get_url_query_str(r,query,n+1)==ESP_OK;char *value=NULL;size_t len=0;
        if(ok){char *p=query;while(p&&*p){char *sep=strchr(p,'&');if(sep)*sep=0;if(strncmp(p,"c=",2)==0){if(value){ok=false;break;}value=p+2;len=strlen(value);}p=sep?sep+1:NULL;}}
        ok=ok&&value&&c6_net_decode(value,len,command,n+1)&&command[0];
        esp_err_t e=ok?runtime_http(r,command,NULL,false):send_error(r,"400 Bad Request","invalid_command");
        memset(query,0,n+1);memset(command,0,n+1);free(query);free(command);return e;
    }
    return send_error(r,"404 Not Found","not_found");
}
static esp_err_t http_provision(httpd_req_t *r) {
    if(r->content_len<=0||r->content_len>1024)return send_error(r,"400 Bad Request","invalid_body_length");
    size_t n=r->content_len;char *cmd=calloc(1,n+11);if(!cmd)return send_error(r,"503 Service Unavailable","no_memory");
    memcpy(cmd,"PROVISION ",10);size_t read=0;bool ok=true;
    while(read<n){int got=httpd_req_recv(r,cmd+10+read,n-read);if(got<=0){ok=false;break;}read+=got;}
    /* Reject embedded raw NUL/control bytes, retaining JSON whitespace only. */
    for(size_t i=0;i<read;++i){unsigned char c=cmd[10+i];if(c==0||(c<32&&c!='\r'&&c!='\n'&&c!='\t'))ok=false;}
    esp_err_t e=ok?runtime_http(r,cmd,NULL,false):send_error(r,"400 Bad Request","invalid_body");
    memset(cmd,0,n+11);free(cmd);return e;
}
static esp_err_t start_http(void) {
    esp_err_t e;
    if(!s_http){
        httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.stack_size=8192;cfg.max_uri_handlers=10;
        cfg.recv_wait_timeout=5;cfg.send_wait_timeout=5;cfg.lru_purge_enable=true;
        e=httpd_start(&s_http,&cfg);if(e!=ESP_OK){s_http=NULL;return e;}
    }
    const char *paths[]={"/ping","/state","/touch","/v1/reusable/choice-signal","/v1/health","/cmd","/setup"};
    /* Retain even a partly registered server on error. Stopping it from the
       owner could deadlock with a just-accepted request waiting for owner. */
    while(s_http_routes<sizeof(paths)/sizeof(paths[0])){
        httpd_uri_t u={.uri=paths[s_http_routes],.method=HTTP_GET,.handler=http_get};
        e=httpd_register_uri_handler(s_http,&u);if(e!=ESP_OK)return e;
        ++s_http_routes;
    }
    if(!s_http_post){
        httpd_uri_t u={.uri="/provision",.method=HTTP_POST,.handler=http_provision};
        e=httpd_register_uri_handler(s_http,&u);if(e!=ESP_OK)return e;s_http_post=true;
    }
    return ESP_OK;
}
