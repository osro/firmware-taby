#include "c6_network.h"
#include "c6_net_helpers.h"
#include "c6_runtime.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define UUID(n) BLE_UUID128_INIT(n,0,0,0,0,0,0,0xa0,0,0x40,0,0,0x59,0x42,0x41,0x54)
static const ble_uuid128_t s_service=UUID(1),s_command=UUID(2),s_event=UUID(3),s_state=UUID(4),s_power=UUID(5);
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static bool s_started,s_advertising,s_subscribed;
static uint16_t s_conn=BLE_HS_CONN_HANDLE_NONE,s_event_handle;
static uint32_t s_generation;
static uint8_t s_addr_type;
static char s_name[32],s_last[512]="READY",s_state_cache[64]="unknown";
static char s_power_cache[128]="{\"power_voltage_mv\":0,\"battery_percent\":-1,\"external_power\":false}";
static QueueHandle_t s_queue;
typedef struct {uint32_t generation;char command[256];} command_job_t;
static void advertise(void);
static void update_status(const char *error) {
    portENTER_CRITICAL(&s_lock);bool started=s_started,adv=s_advertising,conn=s_conn!=BLE_HS_CONN_HANDLE_NONE;portEXIT_CRITICAL(&s_lock);
    c6_network_ble_status(started,adv,conn,error);
}
/* NimBLE notify API takes the host lock itself. No application lock is held
   while calling it, nor while waiting for the display owner. */
static void notify_last(void) {
    char payload[512];uint16_t conn;bool subscribed;
    portENTER_CRITICAL(&s_lock);conn=s_conn;subscribed=s_subscribed;memcpy(payload,s_last,sizeof(payload));portEXIT_CRITICAL(&s_lock);
    if(!subscribed||conn==BLE_HS_CONN_HANDLE_NONE)return;
    const char *text=payload;uint16_t mtu=ble_att_mtu(conn);
    if(mtu<23 || strlen(text)>(size_t)(mtu-3))text="ERR mtu"; /* full reply is readable */
    struct os_mbuf *om=ble_hs_mbuf_from_flat(text,strlen(text));
    if(!om){update_status("ble_notify_no_memory");return;}
    if(ble_gatts_notify_custom(conn,s_event_handle,om)!=0)update_status("ble_notify_failed");
}
static void publish(const char *text,uint32_t generation) {
    portENTER_CRITICAL(&s_lock);
    bool current=generation==s_generation && s_conn!=BLE_HS_CONN_HANDLE_NONE;
    if(current)snprintf(s_last,sizeof(s_last),"%s",text);
    portEXIT_CRITICAL(&s_lock);
    if(current)notify_last();
}
static int access_chr(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg) {
    (void)conn;(void)attr;uintptr_t kind=(uintptr_t)arg;
    if(kind==2){
        if(ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR)return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        uint16_t n=OS_MBUF_PKTLEN(ctx->om);char bytes[256];command_job_t job={0};
        if(!n||n>=sizeof(bytes))return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if(os_mbuf_copydata(ctx->om,0,n,bytes)!=0)return BLE_ATT_ERR_UNLIKELY;
        if(!c6_net_command_bytes(bytes,n,job.command,sizeof(job.command))){memset(bytes,0,sizeof(bytes));return BLE_ATT_ERR_VALUE_NOT_ALLOWED;}
        portENTER_CRITICAL(&s_lock);job.generation=s_generation;portEXIT_CRITICAL(&s_lock);
        BaseType_t ok=xQueueSend(s_queue,&job,0);memset(bytes,0,sizeof(bytes));memset(&job,0,sizeof(job));
        /* ATT success means accepted; final OK/ERR arrives through event. */
        return ok==pdTRUE?0:BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if(ctx->op!=BLE_GATT_ACCESS_OP_READ_CHR)return BLE_ATT_ERR_READ_NOT_PERMITTED;
    char text[512];
    portENTER_CRITICAL(&s_lock);
    snprintf(text,sizeof(text),"%s",kind==3?s_last:kind==4?s_state_cache:s_power_cache);
    portEXIT_CRITICAL(&s_lock);
    return os_mbuf_append(ctx->om,text,strlen(text))==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&s_service.u,.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&s_command.u,.access_cb=access_chr,.arg=(void*)2,.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP},
        {.uuid=&s_event.u,.access_cb=access_chr,.arg=(void*)3,.val_handle=&s_event_handle,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY},
        {.uuid=&s_state.u,.access_cb=access_chr,.arg=(void*)4,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=&s_power.u,.access_cb=access_chr,.arg=(void*)5,.flags=BLE_GATT_CHR_F_READ},
        {0}}}, {0}
};
static int gap_event(struct ble_gap_event *event,void *arg) {
    (void)arg;
    switch(event->type){
        case BLE_GAP_EVENT_CONNECT:
            portENTER_CRITICAL(&s_lock);s_advertising=false;
            if(event->connect.status==0){s_conn=event->connect.conn_handle;s_subscribed=false;++s_generation;strcpy(s_last,"READY");}
            portEXIT_CRITICAL(&s_lock);
            update_status(event->connect.status?"ble_connect_failed":NULL);
            if(event->connect.status)advertise();
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            portENTER_CRITICAL(&s_lock);s_conn=BLE_HS_CONN_HANDLE_NONE;s_subscribed=false;s_advertising=false;++s_generation;portEXIT_CRITICAL(&s_lock);
            update_status(NULL);advertise();break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if(event->subscribe.attr_handle==s_event_handle){
                portENTER_CRITICAL(&s_lock);s_subscribed=event->subscribe.cur_notify!=0;portEXIT_CRITICAL(&s_lock);notify_last();
            }break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            portENTER_CRITICAL(&s_lock);s_advertising=false;portEXIT_CRITICAL(&s_lock);update_status(NULL);advertise();break;
        default:break;
    }
    return 0;
}
static void advertise(void) {
    struct ble_hs_adv_fields f={0};
    f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128=(ble_uuid128_t*)&s_service;f.num_uuids128=1;f.uuids128_is_complete=1;
    int rc=ble_gap_adv_set_fields(&f);
    struct ble_hs_adv_fields rsp={0};rsp.name=(uint8_t*)s_name;rsp.name_len=strlen(s_name);rsp.name_is_complete=1;
    if(!rc)rc=ble_gap_adv_rsp_set_fields(&rsp);
    struct ble_gap_adv_params p={0};p.conn_mode=BLE_GAP_CONN_MODE_UND;p.disc_mode=BLE_GAP_DISC_MODE_GEN;
    if(!rc)rc=ble_gap_adv_start(s_addr_type,NULL,BLE_HS_FOREVER,&p,gap_event,NULL);
    portENTER_CRITICAL(&s_lock);s_advertising=rc==0;portEXIT_CRITICAL(&s_lock);
    update_status(rc?"ble_advertising_failed":NULL);
}
static void host_sync(void) {
    if(ble_hs_id_infer_auto(0,&s_addr_type)!=0){update_status("ble_address_failed");return;}
    advertise();
}
static void host_reset(int reason) {
    (void)reason;portENTER_CRITICAL(&s_lock);s_advertising=false;s_conn=BLE_HS_CONN_HANDLE_NONE;s_subscribed=false;++s_generation;portEXIT_CRITICAL(&s_lock);
    update_status("ble_host_reset");
}
static void host_task(void *arg) {(void)arg;nimble_port_run();nimble_port_freertos_deinit();}
static void refresh_cache(void) {
    char response[256];
    if(c6_runtime_request("STATE",response,sizeof(response),500)&&strncmp(response,"TABY:STATE ",11)==0){
        const char *v=response+11;if(strlen(v)<sizeof(s_state_cache)){portENTER_CRITICAL(&s_lock);strcpy(s_state_cache,v);portEXIT_CRITICAL(&s_lock);}
    }
    if(c6_runtime_request("POWER",response,sizeof(response),500)&&strncmp(response,"TABY:POWER ",11)==0){
        const char *v=response+11;if(strlen(v)<sizeof(s_power_cache)){portENTER_CRITICAL(&s_lock);strcpy(s_power_cache,v);portEXIT_CRITICAL(&s_lock);}
    }
}
static void command_task(void *arg) {
    (void)arg;command_job_t job;char response[4096],event[512];
    for(;;){
        if(xQueueReceive(s_queue,&job,pdMS_TO_TICKS(500))==pdTRUE){
            portENTER_CRITICAL(&s_lock);bool current=job.generation==s_generation&&s_conn!=BLE_HS_CONN_HANDLE_NONE;portEXIT_CRITICAL(&s_lock);
            if(current){
                if(!c6_runtime_request(job.command,response,sizeof(response),10000))strcpy(event,"ERR runtime_unavailable");
                else if(!c6_net_frame(response,event,sizeof(event)))strcpy(event,"ERR response_too_large");
                publish(event,job.generation);
            }
            memset(&job,0,sizeof(job));memset(response,0,sizeof(response));memset(event,0,sizeof(event));
        }
        refresh_cache();
    }
}
esp_err_t c6_ble_start(const char *name) {
    if(s_started)return ESP_OK;
    if(!name||strlen(name)>=sizeof(s_name))return ESP_ERR_INVALID_ARG;
    strcpy(s_name,name);esp_err_t e=nimble_port_init();if(e!=ESP_OK){update_status("ble_init_failed");return e;}
    ble_svc_gap_init();ble_svc_gatt_init();
    int rc=ble_gatts_count_cfg(services);if(!rc)rc=ble_gatts_add_svcs(services);if(!rc)rc=ble_svc_gap_device_name_set(s_name);
    if(rc){nimble_port_deinit();update_status("ble_gatt_failed");return ESP_FAIL;}
    s_queue=xQueueCreate(4,sizeof(command_job_t));
    if(!s_queue){nimble_port_deinit();return ESP_ERR_NO_MEM;}
    if(xTaskCreate(command_task,"c6_ble_commands",8192,NULL,4,NULL)!=pdPASS){vQueueDelete(s_queue);s_queue=NULL;nimble_port_deinit();return ESP_ERR_NO_MEM;}
    ble_hs_cfg.sync_cb=host_sync;ble_hs_cfg.reset_cb=host_reset;
    ble_hs_cfg.sm_bonding=0;ble_hs_cfg.sm_mitm=0;ble_hs_cfg.sm_sc=0;
    portENTER_CRITICAL(&s_lock);s_started=true;portEXIT_CRITICAL(&s_lock);
    update_status(NULL);nimble_port_freertos_init(host_task);return ESP_OK;
}
