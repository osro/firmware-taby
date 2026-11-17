#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define C6_NET_MAX_PROFILES 5
/* Byte-only versioned format: no compiler padding or unterminated strings. */
typedef struct { char ssid[33], password[65]; uint8_t priority, preferred, last_success; } c6_net_profile_t;
typedef struct { uint8_t version, count; c6_net_profile_t profiles[C6_NET_MAX_PROFILES]; } c6_net_store_t;
typedef bool (*c6_net_persist_fn)(const void *, size_t, void *);
bool c6_net_credentials_valid(const char *,const char *);
bool c6_net_json_safe(const char *);
bool c6_net_json_no_nul(const char *);
bool c6_net_decode(const char *,size_t,char *,size_t);
bool c6_net_command_bytes(const void *,size_t,char *,size_t);
bool c6_net_frame(const char *,char *,size_t);
bool c6_net_store_valid(const c6_net_store_t *,size_t);
bool c6_net_upsert(c6_net_store_t *,const char *,const char *);
bool c6_net_forget(c6_net_store_t *,const char *);
bool c6_net_prefer(c6_net_store_t *,const char *);
bool c6_net_save(c6_net_store_t *,const c6_net_store_t *,c6_net_persist_fn,void *);
