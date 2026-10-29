#include "ear_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static uint16_t pixels[240*320],buffer[240*20];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data){uint16_t *p=(uint16_t *)data;for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)pixels[y*240+x]=*p++;lv_display_flush_ready(d);}
static void save(const char *dir,const char *name){char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 320\n255\n");for(int i=0;i<240*320;i++){uint16_t p=pixels[i];unsigned char rgb[]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};assert(fwrite(rgb,1,3,f)==3);}fclose(f);}
static void check(const char *s){const unsigned char *p=(const unsigned char *)s;while(*p){uint32_t code=*p++;if(code>=0xe0){code=(code&15)<<12;code|=(*p++&63)<<6;code|=*p++&63;}else if(code>=0xc0){code=(code&31)<<6;code|=*p++&63;}lv_font_glyph_dsc_t d;assert(lv_font_get_glyph_dsc(&ear_font_16,&d,code,0)&&!d.is_placeholder);}}
static void check_labels(lv_obj_t *o){if(lv_obj_check_type(o,&lv_label_class))check(lv_label_get_text(o));for(unsigned i=0;i<lv_obj_get_child_count(o);i++)check_labels(lv_obj_get_child(o,(int)i));}
int main(int argc,char **argv){assert(argc==2);lv_init();lv_display_t *d=lv_display_create(240,320);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);et_ui_create();et_app_t a;et_app_init(&a);a.audio_ok=true;a.battery=86;
    for(int page=0;page<=ET_POOL;page++)for(int mode=0;mode<ET_MODE_COUNT;mode++){
        a.page=page;a.config.mode=mode;et_question(&a.question,&a.config,&a.seed);a.round=1;a.rhythm_percent=87;
        for(int f=0;f<25;f++){a.focus=f;et_ui_refresh(&a,1000000);check_labels(lv_screen_active());}
    }
    a.page=ET_HOME;a.focus=0;et_ui_refresh(&a,0);lv_refr_now(d);save(argv[1],"home");
    a.page=ET_SETUP;a.focus=0;et_ui_refresh(&a,0);lv_refr_now(d);save(argv[1],"setup");
    a.page=ET_PRACTICE;a.config.mode=ET_INTERVAL;a.focus=2;et_question(&a.question,&a.config,&a.seed);et_ui_refresh(&a,0);lv_refr_now(d);save(argv[1],"interval");
    a.question.done=true;a.verdict=ET_CORRECT;a.focus=0;et_ui_refresh(&a,0);lv_refr_now(d);save(argv[1],"feedback");
    a.config.mode=ET_RHYTHM;a.rhythm_done=true;a.taps=8;for(int i=0;i<8;i++)a.errors[i]=(i-4)*21;et_ui_refresh(&a,0);lv_refr_now(d);save(argv[1],"rhythm");
    a.page=ET_SUMMARY;a.session=(et_result_t){.total=10,.first=7,.correct=9};et_ui_refresh(&a,0);lv_refr_now(d);save(argv[1],"summary");a.volume_until=2000000;a.config.volume=100;et_ui_refresh(&a,0);check_labels(lv_screen_active());lv_refr_now(d);save(argv[1],"volume-100");
    a.config.volume=0;et_ui_refresh(&a,0);check_labels(lv_screen_active());lv_refr_now(d);save(argv[1],"volume-muted");
    puts("Rendered real LVGL screens and verified every displayed glyph: PASS");
}
