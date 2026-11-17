/* Headless integration boundary: real upstream parser + LVGL, fake LCD/time. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c6_ui.h"
#include "c6_ui_model.h"
#include "taby_reusable_preview.h"
#include "lvgl.h"
#include "esp_err.h"

extern bool taby_runtime_render_reusable_card(const taby_reusable_card_t *, const char *);
static int width=172, height=320, calls, draw_error;
static int64_t now=1000000;
static uint16_t framebuffer[320*320];
int c6_board_width(void) { return width; }
int c6_board_height(void) { return height; }
int64_t esp_timer_get_time(void) { return now; }
esp_err_t c6_board_draw_rect(int x, int y, int w, int h, const uint8_t *pixels) {
    assert(x>=0 && y>=0 && w>0 && h>0 && x+w<=width && y+h<=height);
    assert(w*h<=320*10);
    ++calls;
    if (draw_error) return ESP_FAIL;
    for (int row=0; row<h; ++row)
        memcpy(framebuffer+(y+row)*width+x, pixels+row*w*2, w*2);
    return ESP_OK;
}
static void command(const char *text) {
    char error[128];
    if (!taby_reusable_preview_render_ui_command(text,error,sizeof(error))) {
        fprintf(stderr,"Command failed: %s: %s\n",text,error); abort();
    }
    assert(c6_ui_active());
    assert(calls>0);
}
static lv_obj_t *find_label(lv_obj_t *obj, const char *text) {
    if (lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj),text)==0) return obj;
    for (unsigned i=0; i<lv_obj_get_child_cnt(obj); ++i) {
        lv_obj_t *found=find_label(lv_obj_get_child(obj,i),text);
        if (found) return found;
    }
    return NULL;
}
static lv_obj_t *visible_label(const char *text) {
    lv_obj_t *label=find_label(lv_scr_act(),text);
    assert(label && !lv_obj_has_flag(label,LV_OBJ_FLAG_HIDDEN));
    lv_area_t area;
    lv_obj_get_coords(label,&area);
    assert(area.x1>=0 && area.y1>=0 && area.x2<width && area.y2<height);
    return label;
}
static void screenshot(const char *folder, int index) {
    if (!folder) return;
    char path[512];
    snprintf(path,sizeof(path),"%s/ui-%dx%d-%02d.ppm",folder,width,height,index);
    FILE *file=fopen(path,"wb"); assert(file);
    fprintf(file,"P6\n%d %d\n255\n",width,height);
    for (int i=0; i<width*height; ++i) {
        unsigned raw=framebuffer[i];
        raw=((raw&255)<<8)|(raw>>8);
        unsigned char rgb[3]={(raw>>11)*255/31,((raw>>5)&63)*255/63,(raw&31)*255/31};
        fwrite(rgb,1,3,file);
    }
    fclose(file);
}
int main(int argc,char **argv) {
    const char *commands[]={
        "UI/title:BUSY", "UI/text:OPEN TABY|USB-C connected",
        "UI/icon:water", "UI/title_icon:DRINK WATER|water",
        "UI/title_icon_row:COFFEE BREAK|coffee",
        "UI/title_icon_subtitle:STAY FOCUSED|target|One thing at a time",
        "UI/title_action:OPEN APP|OPEN|phone",
        "UI/choice_2:SET UP TABY|WIRELESS|USB-C|Choose a connection",
        "UI/timer:FOCUS|65|300|Stay with it|run", "UI/progress:UPLOADING|45|Nearly halfway",
    };
    c6_ui_init();
    for(int rotation=0;rotation<2;++rotation) {
        if(rotation) { width=320; height=172; c6_ui_refresh_orientation(); }
        for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);++i) {
            command(commands[i]);
            now+=100000;
            c6_ui_tick(now);
            screenshot(argc>1 ? argv[1] : NULL,(int)i);
        }
    }
    command("UI/title%FF0000:RED");
    assert(framebuffer[0]==0x00f8);  /* swapped RGB565 red */
    command("UI/icon+FFFFFF:water");
    int icon_pixels=0;
    for (int y=40;y<height-40;++y) for (int x=40;x<width-40;++x)
        if(framebuffer[y*width+x]==0xffff) ++icon_pixels;
    assert(icon_pixels>40); /* actual icon survives alpha decoding and zoom */
    command("UI/timer?long-focus:FOCUS|2|10|Two seconds|run");
    assert(strcmp(c6_ui_state_name(),"UI_LONG_FOCUS")==0);
    visible_label("00:02");
    now+=999999; c6_ui_tick(now); visible_label("00:02");
    now+=1; c6_ui_tick(now); visible_label("00:01");
    /* Orientation rebuild preserves the original countdown epoch. */
    width=172; height=320; c6_ui_refresh_orientation();
    now+=1000000; c6_ui_tick(now); visible_label("00:00");
    command("UI/text~loading_dots/1:CONNECTING...|Please wait");
    visible_label("CONNECTING");
    now+=360000; c6_ui_tick(now); visible_label("CONNECTING.");
    now+=1000000; c6_ui_tick(now); visible_label("CONNECTING...");
    command("UI/title~blink/1:BUSY");
    lv_obj_t *blinking=visible_label("BUSY");
    now+=450000; c6_ui_tick(now); assert(lv_obj_has_flag(blinking,LV_OBJ_FLAG_HIDDEN));
    now+=550000; c6_ui_tick(now); visible_label("BUSY");
    command("UI/choice_2:CHOOSE|YES|NO");
    lv_obj_t *yes=visible_label("YES");
    lv_area_t area; lv_obj_get_coords(yes,&area);
    c6_ui_touch(true,(area.x1+area.x2)/2,(area.y1+area.y2)/2);
    now+=20000; c6_ui_touch(false,0,0);
    taby_reusable_choice_signal_t signal;
    taby_reusable_ui_read_choice_signal(&signal);
    assert(signal.signal==1 && signal.selection==TABY_REUSABLE_CHOICE_1);
    c6_ui_hide();
    taby_reusable_ui_read_choice_signal(&signal);
    assert(signal.signal==1 && signal.selection==TABY_REUSABLE_CHOICE_NONE);
    assert(!c6_ui_active());
    for (int i=0;i<width*height;++i) assert(framebuffer[i]==0);
    int before=calls;
    now+=1000000; c6_ui_tick(now);
    assert(calls==before);
    command("UI/title:KEEP ME");
    char error[128];
    assert(!taby_reusable_preview_render_ui_command("UI/title!greeting*:REJECT",error,sizeof(error)));
    visible_label("KEEP ME");
    assert(!taby_reusable_preview_render_ui_command("UI/icon:unknown-icon",error,sizeof(error)));
    visible_label("KEEP ME");
    char long_title[96]; memset(long_title,'W',95); long_title[95]=0;
    taby_reusable_card_t long_card={.kind=TABY_REUSABLE_CARD_TEXT,
        .title=long_title,.style=taby_reusable_ui_default_style()};
    assert(taby_runtime_render_reusable_card(&long_card,"UI_LONG"));
    lv_obj_t *long_label=visible_label(long_title);
    assert(lv_label_get_long_mode(long_label)==LV_LABEL_LONG_SCROLL_CIRCULAR);
    /* Bounded lifecycle: hundreds of rebuilds must leave the LVGL pool stable. */
    c6_ui_hide();
    lv_mem_monitor_t before_memory, after_memory;
    lv_mem_monitor(&before_memory);
    for (int i=0;i<200;++i) {
        command(commands[i%10]);
        c6_ui_hide();
    }
    lv_mem_monitor(&after_memory);
    assert(after_memory.free_size >= before_memory.free_size);
    draw_error=1;
    assert(!taby_reusable_preview_render_ui_command("UI/title:LCD ERROR",error,sizeof(error)));
    assert(!c6_ui_active());
    return 0;
}
