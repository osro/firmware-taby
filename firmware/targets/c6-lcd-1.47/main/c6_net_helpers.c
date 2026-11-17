#include "c6_net_helpers.h"
#include <string.h>
_Static_assert(sizeof(c6_net_store_t)==507,"profile blob format changed: bump version and migrate");
static bool printable(const char *s,size_t lo,size_t hi) {
    if (!s) return false;
    size_t n=0;
    while (s[n]) { if (n>=hi || (unsigned char)s[n]<32 || (unsigned char)s[n]==127) return false; ++n; }
    return n>=lo;
}
static int hex(unsigned char c) { if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
bool c6_net_credentials_valid(const char *ssid,const char *password) {
    if (!printable(ssid,1,32)||!printable(password,0,64)) return false;
    size_t n=strlen(password);
    if(n==0 || (n>=8 && n<=63))return true;
    if(n!=64)return false;
    for(size_t i=0;i<n;++i)if(hex((unsigned char)password[i])<0)return false;
    return true;
}
/* Detect NUL escape tokens, respecting escaped backslashes. cJSON otherwise
   silently shortens such strings when exposing its C-string values. */
bool c6_net_json_no_nul(const char *json) {
    if(!json)return false;
    for(size_t i=0;json[i];++i)if(json[i]=='\\') {
        ++i;if(!json[i])break;
        if(json[i]=='u' && strncmp(json+i+1,"0000",4)==0)return false;
    }
    return true;
}
bool c6_net_json_safe(const char *json) {
    if(!json)return false;
    size_t n=0;while(json[n]){if(n>=1024)return false;++n;}
    if(!c6_net_json_no_nul(json))return false;
    unsigned depth=0;bool quoted=false;
    for(size_t i=0;i<n;++i) {
        if(quoted && json[i]=='\\'){if(++i>=n)return false;continue;}
        if(json[i]=='"'){quoted=!quoted;continue;}
        if(quoted)continue;
        if(json[i]=='{'||json[i]=='['){if(++depth>8)return false;}
        if(json[i]=='}'||json[i]==']'){if(!depth)return false;--depth;}
    }
    return depth==0&&!quoted;
}
bool c6_net_decode(const char *src,size_t n,char *out,size_t cap) {
    if(!out||!cap)return false;
    out[0]=0;
    if(!src)return false;
    size_t j=0;
    for(size_t i=0;i<n;++i) {
        unsigned char c=src[i];
        if(c=='%') { if(i+2>=n)goto bad; int a=hex(src[i+1]),b=hex(src[i+2]); if(a<0||b<0)goto bad; c=(a<<4)|b; i+=2; }
        else if(c=='+')c=' ';
        if(c<32 || c==127 || j+1>=cap)goto bad;
        out[j++]=(char)c;
    }
    out[j]=0;return true;
bad: out[0]=0;return false;
}
bool c6_net_command_bytes(const void *bytes,size_t n,char *out,size_t cap) {
    if(!out||!cap)return false;
    out[0]=0;
    if(!bytes || n==0 || n>=256)return false;
    const unsigned char *src=bytes;
    while(n && (src[n-1]=='\r'||src[n-1]=='\n'))--n;
    if(!n||n>=cap)return false;
    for(size_t i=0;i<n;++i)if(src[i]<32||src[i]==127)return false;
    memcpy(out,src,n);out[n]=0;return true;
}
bool c6_net_frame(const char *response,char *out,size_t cap) {
    if(!out||!cap)return false;
    out[0]=0;if(!response)return false;
    if(strncmp(response,"TABY:",5)==0)response+=5;
    size_t n=strlen(response);
    while(n&&(response[n-1]=='\r'||response[n-1]=='\n'))--n;
    if(!n||n>=cap)return false;
    memcpy(out,response,n);out[n]=0;return true;
}
bool c6_net_store_valid(const c6_net_store_t *s,size_t n) {
    if(!s||n!=sizeof(*s)||s->version!=1||s->count>C6_NET_MAX_PROFILES)return false;
    unsigned preferred=0;
    for(size_t i=0;i<s->count;++i) {
        const c6_net_profile_t *p=&s->profiles[i];
        if(!memchr(p->ssid,0,sizeof(p->ssid))||!memchr(p->password,0,sizeof(p->password)) ||
           !c6_net_credentials_valid(p->ssid,p->password)||p->preferred>1||p->last_success>1)return false;
        preferred+=p->preferred;
        for(size_t j=0;j<i;++j)if(strcmp(p->ssid,s->profiles[j].ssid)==0)return false;
    }
    return preferred<=1;
}
bool c6_net_prefer(c6_net_store_t *s,const char *ssid) {
    if(!ssid||!c6_net_store_valid(s,sizeof(*s)))return false;
    size_t i=0;for(;i<s->count;++i)if(strcmp(s->profiles[i].ssid,ssid)==0)break;
    if(i==s->count)return false;
    /* Stable compact ranks avoid uint8 priority wrap after repeated preference
       changes; larger values retain upstream's higher-priority meaning. */
    unsigned order[C6_NET_MAX_PROFILES];size_t n=0;
    for(size_t j=0;j<s->count;++j)if(j!=i)order[n++]=j;
    for(size_t a=0;a<n;++a)for(size_t b=a+1;b<n;++b)if(s->profiles[order[b]].priority<s->profiles[order[a]].priority) {
        unsigned t=order[a];order[a]=order[b];order[b]=t;
    }
    for(size_t j=0;j<n;++j)s->profiles[order[j]].priority=j+1;
    for(size_t j=0;j<s->count;++j)s->profiles[j].preferred=j==i;
    s->profiles[i].priority=s->count;
    return true;
}
bool c6_net_upsert(c6_net_store_t *s,const char *ssid,const char *password) {
    if(!c6_net_credentials_valid(ssid,password)||!c6_net_store_valid(s,sizeof(*s)))return false;
    size_t i=0;for(;i<s->count;++i)if(strcmp(s->profiles[i].ssid,ssid)==0)break;
    if(i==C6_NET_MAX_PROFILES)return false;
    if(i==s->count) { memset(&s->profiles[i],0,sizeof(s->profiles[i]));++s->count; }
    strcpy(s->profiles[i].ssid,ssid);strcpy(s->profiles[i].password,password);
    s->profiles[i].last_success=0;
    return c6_net_prefer(s,ssid);
}
bool c6_net_forget(c6_net_store_t *s,const char *ssid) {
    if(!ssid||!c6_net_store_valid(s,sizeof(*s)))return false;
    for(size_t i=0;i<s->count;++i)if(strcmp(s->profiles[i].ssid,ssid)==0) {
        memmove(&s->profiles[i],&s->profiles[i+1],(s->count-i-1)*sizeof(s->profiles[0]));
        memset(&s->profiles[--s->count],0,sizeof(s->profiles[0]));return true;
    }
    return false;
}
bool c6_net_save(c6_net_store_t *live,const c6_net_store_t *next,c6_net_persist_fn persist,void *ctx) {
    if(!live||!persist||!c6_net_store_valid(next,sizeof(*next))||!persist(next,sizeof(*next),ctx))return false;
    *live=*next;return true;
}
