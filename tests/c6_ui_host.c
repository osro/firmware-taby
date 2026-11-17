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

static unsigned line_count(lv_obj_t *obj, bool visible) {
    unsigned count=lv_obj_check_type(obj,&lv_line_class) &&
        (!visible || !lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN));
    for (unsigned i=0;i<lv_obj_get_child_cnt(obj);++i)
        count+=line_count(lv_obj_get_child(obj,i),visible);
    return count;
}
static uint32_t pixels_hash(void) {
    uint32_t hash=2166136261U;
    for(int i=0;i<width*height;++i) hash=(hash^framebuffer[i])*16777619U;
    return hash;
}
static void decor_tests(const char *folder) {
    const char *names[]={"none","shooting_stars","firework_dots","heart_burst","gold_rings",
        "square_confetti","diamond_burst","comet_burst","ribbon_sweep","ember_drift",
        "star_twinkle","bubble_pop","pixel_burst","glow_wave","sunbeam_lines",
        "prism_shards","heart_rain","confetti_rain","orbit_dots","flare_fan",
        "petal_swirl","neon_arc","spiral_sparks","checker_burst","aurora_sweep"};
    const int family[]={0,1,2,3,4,2,2,7,8,2,1,4,2,24,1,24,16,2,2,7,3,4,2,2,24};
    static uint16_t baseline[320*320];
    size_t peak_decor_bytes=0;
    for(int orientation=0;orientation<2;++orientation) {
        width=orientation ? 320 : 172; height=orientation ? 172 : 320;
        c6_ui_refresh_orientation();
        command("UI/text:CELEBRATE|You did it");
        memcpy(baseline,framebuffer,sizeof(baseline));
        uint32_t plain=pixels_hash(), hashes[25]={0};
        lv_mem_monitor_t plain_mem;
        lv_mem_monitor(&plain_mem);
        for(int effect=1;effect<=24;++effect) {
            char text[128];
            snprintf(text,sizeof(text),"UI/text&%s/2:CELEBRATE|You did it",names[effect]);
            command(text);
            assert(line_count(lv_scr_act(),false)==8);
            assert(line_count(lv_scr_act(),true)==8);
            visible_label("CELEBRATE"); visible_label("You did it");
            uint32_t first=pixels_hash();
            assert(first!=plain);
            hashes[effect]=first;
            /* Opaque text pixels remain in front of every decoration. */
            for(int i=0;i<width*height;++i)
                if(baseline[i]==0xffff) assert(framebuffer[i]==0xffff);
            lv_mem_monitor_t active_mem, tick_mem;
            lv_mem_monitor(&active_mem);
            size_t decor_bytes=plain_mem.free_size-active_mem.free_size;
            assert(decor_bytes < 8192);
            if(decor_bytes>peak_decor_bytes) peak_decor_bytes=decor_bytes;
            bool canonical=effect==family[effect];
            if(canonical) screenshot(folder,100+effect*10);
            now+=400000; c6_ui_tick(now);
            assert(pixels_hash()!=first && pixels_hash()!=plain);
            if(canonical) screenshot(folder,101+effect*10);
            now+=500000; c6_ui_tick(now);
            if(canonical) screenshot(folder,102+effect*10);
            now+=660000; c6_ui_tick(now);
            if(canonical) screenshot(folder,103+effect*10);
            /* Wraparound returns to precisely the same geometry. */
            now+=40000; c6_ui_tick(now);
            assert(pixels_hash()==first);
            lv_mem_monitor(&tick_mem);
            assert(tick_mem.free_size==active_mem.free_size);
            now+=400000; c6_ui_tick(now);
            assert(c6_ui_active() && pixels_hash()==plain);
            assert(line_count(lv_scr_act(),true)==0);
            if(canonical) screenshot(folder,104+effect*10);
        }
        for(int effect=1;effect<=24;++effect) assert(hashes[effect]==hashes[family[effect]]);
        const int canonical[]={1,2,3,4,7,8,16,24};
        for(unsigned i=0;i<8;++i) for(unsigned j=0;j<i;++j)
            assert(hashes[canonical[i]]!=hashes[canonical[j]]);
    }
    taby_reusable_card_t card={.kind=TABY_REUSABLE_CARD_HEADLINE,.title="KEEP DECOR",
        .style=taby_reusable_ui_default_style(),
        .behavior={.decor_effect=TABY_REUSABLE_DECOR_EFFECT_GOLD_RINGS,.decor_effect_seconds=2}};
    assert(taby_runtime_render_reusable_card(&card,"DECOR_STATE"));
    uint32_t before=pixels_hash();
    card.behavior.decor_effect=-1;
    assert(!taby_runtime_render_reusable_card(&card,"BAD"));
    assert(strcmp(c6_ui_state_name(),"DECOR_STATE")==0 && pixels_hash()==before);
    card.behavior.decor_effect=25;
    assert(!taby_runtime_render_reusable_card(&card,"BAD"));
    assert(pixels_hash()==before);
    card.behavior.decor_effect=TABY_REUSABLE_DECOR_EFFECT_GOLD_RINGS;
    card.kind=TABY_REUSABLE_CARD_ACTION;
    assert(!taby_runtime_render_reusable_card(&card,"BAD"));
    assert(pixels_hash()==before);
    card.kind=TABY_REUSABLE_CARD_HEADLINE;
    card.behavior.decor_effect_seconds=0;
    assert(taby_runtime_render_reusable_card(&card,"ZERO"));
    assert(line_count(lv_scr_act(),false)==0);
    card.behavior.decor_effect_seconds=UINT32_MAX;
    int64_t start=now;
    assert(taby_runtime_render_reusable_card(&card,"LARGE"));
    now=start+(uint64_t)UINT32_MAX*1000000-1; c6_ui_tick(now);
    assert(line_count(lv_scr_act(),true)==8);
    ++now; c6_ui_tick(now);
    assert(c6_ui_active() && line_count(lv_scr_act(),true)==0);
    /* Two rotations preserve the start epoch, including an expired rebuild. */
    command("UI/title&heart_rain/2:ROTATING");
    now+=1500000; c6_ui_tick(now);
    width=172; height=320; c6_ui_refresh_orientation();
    now+=499999; c6_ui_tick(now);
    assert(line_count(lv_scr_act(),true)==8);
    ++now; c6_ui_tick(now);
    assert(c6_ui_active() && line_count(lv_scr_act(),true)==0);
    width=320; height=172; c6_ui_refresh_orientation();
    assert(line_count(lv_scr_act(),false)==0);
    /* Parser keeps its four-second default for omitted/zero duration. */
    command("UI/title&shooting_stars/0:DEFAULT");
    now+=3999999; c6_ui_tick(now);
    assert(line_count(lv_scr_act(),true)==8);
    ++now; c6_ui_tick(now);
    assert(line_count(lv_scr_act(),true)==0);
    /* Text animation and decoration each expire without hiding the card. */
    command("UI/title&gold_rings/2~blink/1:BLINK DECOR");
    lv_obj_t *blink=visible_label("BLINK DECOR");
    now+=450000; c6_ui_tick(now);
    assert(lv_obj_has_flag(blink,LV_OBJ_FLAG_HIDDEN) && line_count(lv_scr_act(),true)==8);
    now+=1550000; c6_ui_tick(now);
    visible_label("BLINK DECOR");
    assert(line_count(lv_scr_act(),true)==0);
    c6_ui_hide();
    lv_mem_monitor_t before_mem, after_mem;
    lv_mem_monitor(&before_mem);
    for(int i=0;i<384;++i) {
        char text[128];
        snprintf(text,sizeof(text),"UI/title&%s/2:REPLACE",names[1+i%24]);
        command(text);
        now+=200000; c6_ui_tick(now);
        if(i%3==0) {
            int old=width; width=height; height=old; c6_ui_refresh_orientation();
            assert(line_count(lv_scr_act(),false)==8);
        }
        if(i%2==0) c6_ui_hide();
    }
    c6_ui_hide();
    lv_mem_monitor(&after_mem);
    assert(after_mem.free_size>=before_mem.free_size);
    fprintf(stderr,"Decor LVGL peak additional retained heap: %zu bytes; lifecycle free: %zu -> %zu\n",
            peak_decor_bytes,(size_t)before_mem.free_size,(size_t)after_mem.free_size);
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
    decor_tests(argc>1 ? argv[1] : NULL);
    draw_error=1;
    assert(!taby_reusable_preview_render_ui_command("UI/title:LCD ERROR",error,sizeof(error)));
    assert(!c6_ui_active());
    return 0;
}
