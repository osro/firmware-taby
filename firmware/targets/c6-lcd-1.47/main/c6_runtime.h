#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Thread-safe transport entry. Commands execute on the display owner, never on
   HTTP/BLE worker tasks. A protocol ERR is a completed request (true); false
   means timeout, queue/memory exhaustion or a response that did not fit. */
bool c6_runtime_request(const char *command,char *response,size_t capacity,uint32_t timeout_ms);
