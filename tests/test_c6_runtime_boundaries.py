"""Compile real owner functions with only RTOS/media boundaries substituted.

The C6 application is not host-linkable as a whole. Extract complete production
function definitions so these tests exercise its response accumulator and startup
selection, without maintaining another implementation of either behavior.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CODE = ROOT / 'firmware/targets/c6-lcd-1.47/main'
SOURCE = CODE / 'app_main.c'


def function(source, name):
    start = re.search(r'^(?:static )?[^\n;]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M).start()
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class C6RuntimeBoundaryTests(unittest.TestCase):
    def run_c(self, code):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / 'driver.c').write_text(code)
            executable = path / 'driver'
            subprocess.run(['cc', '-std=c11', '-D_DARWIN_C_SOURCE', '-Wall', '-Wextra',
                            '-Werror', f'-I{CODE}', str(path / 'driver.c'), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_full_sd_catalog_fits_actual_queued_request_boundary(self):
        source = SOURCE.read_text()
        request_type = re.search(r'typedef struct \{\s*char .*?\} c6_request_t;', source, re.S).group()
        catalog_body = source.split('} else if (strcmp(line, "ANIMATIONS") == 0) {', 1)[1].split('} else if', 1)[0]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generated/c6_catalog.h"
#include "generated/c6_sd_catalog.h"
typedef int *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static int request_lock;
static QueueHandle_t requests=(void*)1;
static TaskHandle_t owner=(void*)1;
static unsigned allocations, releases, semaphore_deletes;
static size_t last_allocation;
static bool timeout_wait;
static void *tracked_calloc(size_t n,size_t size){++allocations;last_allocation=n*size;return calloc(n,size);}
static void tracked_free(void *p){++releases;free(p);}
#define calloc tracked_calloc
#define free tracked_free
static SemaphoreHandle_t xSemaphoreCreateBinary(void){return (void*)1;}
static void vSemaphoreDelete(SemaphoreHandle_t s){assert(s);++semaphore_deletes;}
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void*)2;}
static int usb_serial_jtag_write_bytes(const void *p,size_t n,unsigned t){(void)p;(void)n;(void)t;abort();}
static bool c6_sd_ready(void){return true;}
static size_t c6_sd_count(void){return C6_SD_CLIP_COUNT;}
static const c6_clip_t *c6_sd_at(size_t i){return &c6_sd_clips[i];}
'''
        code += request_type + '\nstatic c6_request_t *active_request,*queued;\n'
        code += 'static void handle_command(const char *line);\n'
        code += function(source, 'release_request') + '\n'
        code += function(source, 'execute_request') + '\n'
        code += r'''
static int xQueueSend(QueueHandle_t q,c6_request_t **r,unsigned t){(void)q;(void)t;queued=*r;return pdTRUE;}
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned t){
    (void)s;(void)t;
    if(timeout_wait)return 0;
    execute_request(queued);release_request(queued);queued=NULL;return pdTRUE;
}
'''
        code += function(source, 'c6_runtime_request') + '\n' + function(source, 'reply')
        code += '\nstatic void handle_command(const char *line) {\nchar response[2048];\nassert(!strcmp(line,"ANIMATIONS"));\n'
        code += catalog_body + '\n}\n'
        code += r'''
int main(void){
    char response[4096];
    assert(c6_runtime_request("ANIMATIONS",response,sizeof(response),100));
    assert(!strncmp(response,"TABY:ANIMATIONS_BEGIN\n",22));
    assert(strlen(response)>2048 && strlen(response)<sizeof(response));
    unsigned count=0;
    for(char *p=response;(p=strstr(p,"TABY:ANIMATION "));++p)++count;
    assert(count==84 && strstr(response,"\nTABY:ANIMATIONS_END"));
    assert(allocations==1 && releases==1 && semaphore_deletes==1);
    timeout_wait=true;
    assert(!c6_runtime_request("ANIMATIONS",response,sizeof(response),1));
    assert(allocations==2 && releases==1); /* queued owner still owns request */
    execute_request(queued);release_request(queued);queued=NULL;
    assert(releases==2 && semaphore_deletes==2);
    timeout_wait=false;
    char small[64];
    assert(!c6_runtime_request("ANIMATIONS",small,sizeof(small),100));
    assert(!strcmp(small,"TABY:ERR runtime_unavailable"));
    assert(last_allocation<512); /* short caller must not reserve a 4 KB reply */
    assert(releases==3 && semaphore_deletes==3);
    char large[8192];
    assert(c6_runtime_request("ANIMATIONS",large,sizeof(large),100));
    assert(last_allocation>4096 && last_allocation<4608); /* reply capped at 4 KB */
    struct {char text[2];unsigned char guard;} tiny={{0},0xa5};
    assert(!c6_runtime_request("ANIMATIONS",tiny.text,sizeof(tiny.text),100));
    assert(tiny.text[1]=='\0' && tiny.guard==0xa5);
    assert(releases==5 && semaphore_deletes==5);
    char oversized[1025];memset(oversized,'X',sizeof(oversized));oversized[1024]='\0';
    assert(!c6_runtime_request(oversized,small,sizeof(small),100));
    assert(allocations==5);
    return 0;
}
'''
        self.run_c(code)

    def test_single_and_multiple_reply_lines_accept_exact_capacity(self):
        source = SOURCE.read_text()
        request_type = re.search(r'typedef struct \{\s*char .*?\} c6_request_t;', source, re.S).group()
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
typedef void *SemaphoreHandle_t;
#define pdMS_TO_TICKS(n) (n)
static int usb_serial_jtag_write_bytes(const void *p,size_t n,unsigned t){(void)p;(void)n;(void)t;abort();}
'''
        code += request_type + '\nstatic c6_request_t *active_request;\n'
        code += function(source, 'reply')
        code += r'''
int main(void){
    struct {char text[16];unsigned char guard;} single={{0},0xa5};
    c6_request_t request={.response=single.text,.response_capacity=sizeof(single.text)};
    active_request=&request;
    reply("TABY:STATE IDLE");
    assert(!request.overflow && request.used==15);
    assert(!strcmp(single.text,"TABY:STATE IDLE") && single.guard==0xa5);
    struct {char text[4];unsigned char guard;} multiple={{0},0xa5};
    request=(c6_request_t){.response=multiple.text,.response_capacity=sizeof(multiple.text)};
    reply("A");reply("B");
    assert(!request.overflow && request.used==3);
    assert(!strcmp(multiple.text,"A\nB") && multiple.guard==0xa5);
    reply("C");
    assert(request.overflow && !strcmp(multiple.text,"A\nB") && multiple.guard==0xa5);
    return 0;
}
'''
        self.run_c(code)

    def test_voice_card_handoff_preserves_card_and_installs_idle_backing(self):
        source = SOURCE.read_text()
        ui_start = source.index('            if(taby_reusable_preview_render_ui_command(')
        ui_end = source.index('        } else {', ui_start)
        ui_branch = source[ui_start:ui_end]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {const char *id;} c6_clip_t;
static const c6_clip_t idle={"idle_01_loop"};
static const c6_clip_t *clip,*sequence_next;
static bool voice_mode=true,voice_active=true,ui_active;
static struct {int current_state;} machine={2};
#define TABY_STATE_AMBIENT_IDLE 0
static bool select_clip_id(const char *id){assert(!strcmp(id,"idle_01_loop"));clip=&idle;return true;}
static void c6_voice_reset(void){voice_active=false;}
static bool c6_board_healthy(void){return true;}
static const char *state_name(void){return ui_active?"UI_TIMER":"IDLE";}
static bool taby_reusable_preview_render_ui_command(const char *command,char *error,size_t size){
    (void)error;(void)size;assert(!strcmp(command,"UI/timer:FOCUS|1|1||run|1"));
    ui_active=true;return true;
}
'''
        code += function(source, 'idle_backing')
        code += '\nint main(void){\nconst char *command="UI/timer:FOCUS|1|1||run|1";\nchar error[128],response[256];\n'
        code += ui_branch
        code += r'''
    assert(ui_active && !strcmp(response,"TABY:OK UI_TIMER"));
    assert(!voice_mode && !voice_active && clip==&idle && !sequence_next);
    assert(machine.current_state==TABY_STATE_AMBIENT_IDLE);
    return 0;
}
'''
        self.run_c(code)

    def test_startup_read_failure_after_sd_validation_retries_flash(self):
        source = SOURCE.read_text()
        selection = re.search(r'^    ESP_ERROR_CHECK\([^\n]*"startup"[^\n]*;', source, re.M).group()
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generated/c6_catalog.h"
#include "generated/c6_sd_catalog.h"
#define C6_LCD_WIDTH 172
#define C6_FRAME_HEIGHT 280
#define ESP_OK 0
#define ESP_ERR_INVALID_SIZE 1
#define ESP_ERROR_CHECK(result) assert((result)==ESP_OK)
typedef struct {const char *animation_id;} taby_animation_asset_t;
static bool taby_animation_asset_for_id(const char *id,taby_animation_asset_t *out){(void)id;(void)out;return false;}
static bool sd_ready=true,display_fault,fail_read=true;
static unsigned header_reads;
static uint8_t *assets;
static const c6_clip_t *clip;
static size_t cursor;
static uint16_t frame,clip_frames;
static bool clip_loop;
static int64_t due;
static bool c6_board_healthy(void){return true;}
static bool c6_sd_owns(const c6_clip_t *p){
    for(size_t i=0;i<C6_SD_CLIP_COUNT;++i)if(p==&c6_sd_clips[i])return true;
    return false;
}
static size_t c6_sd_count(void){return sd_ready?C6_SD_CLIP_COUNT:0;}
static const c6_clip_t *c6_sd_at(size_t i){return &c6_sd_clips[i];}
static void c6_sd_disable(void){sd_ready=false;}
static const uint8_t *c6_sd_read(const c6_clip_t *p,size_t offset,size_t n){
    assert(c6_sd_owns(p) && offset==0 && n==12);++header_reads;
    if(fail_read){c6_sd_disable();return NULL;} /* CRC passed; first subsequent read fails */
    static const uint8_t bad_header[12]={0};return bad_header;
}
'''
        for name in ('u16', 'clip_data', 'find_clip', 'select_clip'):
            code += function(source, name) + '\n'
        if re.search(r'\bselect_clip_id\(', source):
            code += function(source, 'select_clip_id') + '\n'
        code += r'''
int main(void){
    assets=calloc(1,C6_ASSET_BYTES);assert(assets);
    const uint8_t header[]={ 'T','C','6','A',172,0,24,1,1,0,0,0 };
    memcpy(assets,header,sizeof(header));
'''
        code += selection + '\n'
        code += r'''
    assert(header_reads==1 && !sd_ready);
    assert(clip==&c6_clips[0] && !strcmp(clip->id,"startup"));
    assert(cursor==12 && frame==0 && clip_frames==1 && !clip_loop && due==0);
    sd_ready=true;fail_read=false;clip=NULL;
'''
        code += selection + '\n'
        code += r'''
    assert(header_reads==2 && !sd_ready && clip==&c6_clips[0]);
    free(assets);return 0;
}
'''
        self.run_c(code)


if __name__ == '__main__':
    unittest.main()
