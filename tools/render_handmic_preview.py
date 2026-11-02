#!/usr/bin/env python3
"""Compile product UI with real host LVGL; verify glyphs and render reference states."""
from pathlib import Path
import os
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
CMAKE = r'''
cmake_minimum_required(VERSION 3.16)
project(handmic_preview C)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_DIR}" lvgl)
add_executable(handmic_preview preview.c "${APP_DIR}/hm_ui.c" "${APP_DIR}/hm_product.c" "${APP_DIR}/hm_core.c" "${APP_DIR}/hm_adpcm.c" "${CJSON_DIR}/cJSON.c" "${APP_DIR}/hm_font_16.c")
target_include_directories(handmic_preview PRIVATE "${APP_DIR}" "${CJSON_DIR}")
target_link_libraries(handmic_preview PRIVATE lvgl m)
target_compile_definitions(handmic_preview PRIVATE HM_UI_TESTING=1)
target_compile_options(handmic_preview PRIVATE -Wall -Wextra -Werror)
'''
CONF = r'''
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_USE_OS LV_OS_NONE
#define LV_MEM_SIZE (40 * 1024)
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_USE_THORVG_INTERNAL 0
#define LV_BUILD_DEMOS 0
#define LV_BUILD_EXAMPLES 0
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_OBJ 1
#endif
'''
HARNESS = r'''
#include "hm_ui.h"
#include "hm_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t pixels[240*320], buffer[240*20];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data) {
 uint16_t *p=(uint16_t *)data;
 for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)pixels[y*240+x]=*p++;
 lv_display_flush_ready(d);
}
static void check_labels(lv_obj_t *o) {
 if(lv_obj_check_type(o,&lv_label_class)) {
  const unsigned char *p=(const unsigned char *)lv_label_get_text(o);
  while(*p) {
   uint32_t code=*p++;
   if(code>=0xe0){code=(code&15)<<12;code|=(*p++&63)<<6;code|=*p++&63;}
   else if(code>=0xc0){code=(code&31)<<6;code|=*p++&63;}
   if(code=='\n')continue;
   lv_font_glyph_dsc_t glyph;
   const lv_font_t *font=lv_obj_get_style_text_font(o,LV_PART_MAIN);
   if(!lv_font_get_glyph_dsc(font,&glyph,code,0)||glyph.is_placeholder) { fprintf(stderr,"Missing U+%04x in %s font=%p hm=%p 12=%p 32=%p\n",(unsigned)code,lv_label_get_text(o),(void*)font,(void*)&hm_font_16,(void*)&lv_font_montserrat_12,(void*)&lv_font_montserrat_32);abort(); }
  }
 }
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)check_labels(lv_obj_get_child(o,(int)i));
}
static void check_bounds(lv_obj_t *o) {
 if(lv_obj_is_hidden(o))return;
 lv_area_t a;lv_obj_get_coords(o,&a);
 if(a.x1<0||a.y1<0||a.x2>=240||a.y2>=320) {
  fprintf(stderr,"Object outside screen: (%d,%d)-(%d,%d)\n",(int)a.x1,(int)a.y1,(int)a.x2,(int)a.y2);abort();
 }
 if(lv_obj_check_type(o,&lv_label_class)&&lv_label_get_long_mode(o)==LV_LABEL_LONG_WRAP) {
  lv_point_t size;const char *value=lv_label_get_text(o);const lv_font_t *font=lv_obj_get_style_text_font(o,LV_PART_MAIN);
  lv_text_get_size(&size,value,font,lv_obj_get_style_text_letter_space(o,LV_PART_MAIN),lv_obj_get_style_text_line_space(o,LV_PART_MAIN),lv_obj_get_width(o),LV_TEXT_FLAG_NONE);
  if(size.y>lv_obj_get_height(o)) {fprintf(stderr,"Text clipped: %s, height=%d available=%d\n",value,(int)size.y,(int)lv_obj_get_height(o));abort();}
 }
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)check_bounds(lv_obj_get_child(o,(int)i));
}
static bool label_contains(lv_obj_t *o,const char *needle) {
 if(lv_obj_is_hidden(o))return false;
 if(lv_obj_check_type(o,&lv_label_class)&&strstr(lv_label_get_text(o),needle))return true;
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)if(label_contains(lv_obj_get_child(o,(int)i),needle))return true;
 return false;
}
static void check_full_call(lv_obj_t *o,const char *call) {
 if(!lv_obj_is_hidden(o)&&lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),call)) {
  assert(lv_label_get_long_mode(o)==LV_LABEL_LONG_WRAP);
  lv_point_t size;const lv_font_t *font=lv_obj_get_style_text_font(o,LV_PART_MAIN);
  lv_text_get_size(&size,call,font,0,1,lv_obj_get_width(o),LV_TEXT_FLAG_NONE);
  assert(size.y<=(int)font->line_height*2+1);
 }
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)check_full_call(lv_obj_get_child(o,(int)i),call);
}
static void save(lv_display_t *d,const char *dir,const char *name,hm_ui_snapshot_t *s) {
 fprintf(stderr,"Render %s\n",name);
 hm_ui_render(s); lv_obj_update_layout(lv_screen_active()); check_labels(lv_screen_active()); check_bounds(lv_screen_active());lv_refr_now(d);
 lv_mem_monitor_t mem;lv_mem_monitor(&mem);fprintf(stderr,"LVGL free=%zu largest=%zu maxused=%zu\n",mem.free_size,mem.free_biggest_size,mem.max_used);
 char path[1024]; snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);
 FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n240 320\n255\n");
 for(int i=0;i<240*320;i++){uint16_t p=pixels[i]; unsigned char rgb[]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};assert(fwrite(rgb,1,3,f)==3);} fclose(f);
}
static unsigned command_id;
static void real_command(hm_core_t *c,const char *op,const char *params,uint64_t now) {
 char json[513],out[513];snprintf(json,sizeof json,"{\"v\":1,\"id\":%u,\"sid\":%u,\"op\":\"%s\",\"p\":%s}",++command_id,!strcmp(op,"hello")?0:c->s.sid,op,params);
 assert(hm_core_command(c,json,strlen(json),now,out));if(!strstr(out,"\"ok\":true")){fprintf(stderr,"core request failed %s: %s\n",op,out);abort();}
}
static void real_radio(hm_core_t *c,const char *network,bool muted,uint64_t now) {
 char p[300];snprintf(p,sizeof p,"{\"context_id\":1,\"network\":\"%s\",\"callsign\":\"BI8SYN\",\"server\":\"成都测试\",\"phase\":\"idle\",\"muted\":%s,\"tx_allowed\":%s}",network,muted?"true":"false",!strcmp(network,"online")?"true":"false");real_command(c,"radio.update",p,now);
}
static void real_setup(hm_core_t *c) {
 hm_core_init(c,"test-device",90);hm_core_open(c,244,0);real_command(c,"hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":244}",0);real_radio(c,"online",false,1);
}
static void real_snapshot(hm_core_t *c,hm_ui_snapshot_t *s,uint64_t now) {
 hm_snapshot_t core;hm_core_get_snapshot(c,now,&core);hm_product_observe(&s->p,&core,true,true,false,now);s->now_ms=now;
 s->message[0]=0;hm_ui_set_phase(s);
}
static void regression_network(lv_display_t *d,const char *dir) {
 hm_core_t c;hm_ui_snapshot_t s={0};hm_product_init(&s.p,90,"phosphor_green",(hm_product_store_t){0});real_setup(&c);
 real_snapshot(&c,&s,2);save(d,dir,"real-network-online",&s);assert(label_contains(lv_screen_active(),"守听"));
 real_radio(&c,"offline",false,3);assert(c.s.ready);real_snapshot(&c,&s,3);save(d,dir,"real-network-offline",&s);
 assert(label_contains(lv_screen_active(),"请在App中连接服务器"));assert(!label_contains(lv_screen_active(),"守听"));
 real_radio(&c,"connecting",false,4);assert(c.s.ready);real_snapshot(&c,&s,4);save(d,dir,"real-network-connecting",&s);assert(label_contains(lv_screen_active(),"请在App中连接服务器"));
 real_radio(&c,"online",false,5);real_snapshot(&c,&s,5);save(d,dir,"real-network-recovered",&s);assert(label_contains(lv_screen_active(),"守听"));
}
static void regression_muted(lv_display_t *d,const char *dir) {
 hm_core_t c;hm_ui_snapshot_t s={0};hm_product_init(&s.p,90,"phosphor_green",(hm_product_store_t){0});real_setup(&c);
 real_command(&c,"talker.update","{\"context_id\":1,\"turn_id\":1,\"uid\":11,\"callsign\":\"BH8ABC\",\"active\":true}",2);
 real_command(&c,"stream.start","{\"stream_id\":1,\"direction\":\"speaker\",\"press_id\":0,\"turn_id\":1}",3);
 real_snapshot(&c,&s,3);save(d,dir,"real-app-unmuted",&s);assert(!label_contains(lv_screen_active(),"静音"));
 real_radio(&c,"online",true,4);assert(c.s.muted&&c.s.direction==HM_DIRECTION_NONE&&c.s.volume==90);
 real_snapshot(&c,&s,4);save(d,dir,"real-app-muted",&s);assert(label_contains(lv_screen_active(),"静音"));assert(label_contains(lv_screen_active(),"请在App中取消静音"));
 real_radio(&c,"online",false,5);real_snapshot(&c,&s,5);save(d,dir,"real-app-unmuted-again",&s);assert(!label_contains(lv_screen_active(),"静音"));
}
static void write32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void regression_ptt(lv_display_t *d,const char *dir) {
 hm_core_t c;hm_ui_snapshot_t s={0};char out[513];hm_product_init(&s.p,90,"phosphor_green",(hm_product_store_t){0});
 const char *reasons[]={"busy","permission_unknown","timeout","tx_timeout"};
 const char *labels[]={"有人正在讲话","暂时无法讲话","暂时无法讲话","本次讲话已结束"};
 for(unsigned n=0;n<4;n++) {
  real_setup(&c);assert(hm_core_press(&c,true,2,out));
  uint64_t now=4;
  if(n<2) {char p[100];snprintf(p,sizeof p,"{\"press_id\":1,\"reason\":\"%s\"}",reasons[n]);real_command(&c,"ptt.reject",p,now);}
  else if(n==2){now=1002;hm_core_tick(&c,now,out);}
  else {
   real_command(&c,"stream.start","{\"stream_id\":1,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",3);
   uint8_t flow[24]={1};write32(flow+4,c.s.sid);write32(flow+8,1);write32(flow+16,32);
   /* Keep the real credit/session leases alive through the exact 60 s boundary. */
   for(now=4;now<60003;now+=100)assert(hm_core_on_flow(&c,flow,sizeof flow,now));
   now=60003;hm_core_tick(&c,now,out);
  }
  assert(!strcmp(c.s.stop_reason,reasons[n]));assert(c.s.wait_release&&c.s.physical_ptt&&c.s.direction==HM_DIRECTION_NONE);
  real_snapshot(&c,&s,now);char name[50];snprintf(name,sizeof name,"real-ptt-%s",reasons[n]);save(d,dir,name,&s);assert(label_contains(lv_screen_active(),labels[n]));
  assert(s.phase==HM_UI_ERROR);hm_core_press(&c,false,now+1,out);assert(!c.s.stop_reason[0]&&!c.s.rejected&&!c.s.wait_release);
  real_snapshot(&c,&s,now+1);save(d,dir,"real-ptt-released",&s);assert(s.phase==HM_UI_IDLE&&!label_contains(lv_screen_active(),labels[n]));
 }
}
static void regression_pair_code(lv_display_t *d,const char *dir) {
 hm_pair_code_t code={0};hm_snapshot_t core={0};hm_ui_snapshot_t s={.pair_seconds=42};
 hm_product_init(&s.p,90,"phosphor_green",(hm_product_store_t){0});hm_product_observe(&s.p,&core,false,false,true,0);
 hm_product_pair_code_receive(&code,10,10,12034,true,false,true);s.passkey=code.value;s.passkey_valid=code.valid;
 save(d,dir,"real-pair-code",&s);assert(label_contains(lv_screen_active(),"012034"));
 hm_product_pair_code_sync(&code,11,true,false);hm_product_observe(&s.p,&core,false,false,true,1);s.passkey_valid=code.valid;
 save(d,dir,"real-pair-generation-failed",&s);assert(!label_contains(lv_screen_active(),"012034"));assert(label_contains(lv_screen_active(),"等待手机"));
 hm_product_pair_code_sync(&code,12,false,false);hm_product_observe(&s.p,&core,true,false,false,2);s.passkey_valid=code.valid;
 save(d,dir,"real-pair-bond-kept",&s);assert(s.p.bonded&&label_contains(lv_screen_active(),"已保存手机"));
 s.p.page=HM_PAGE_MAIN;hm_product_pair_code_receive(&code,12,12,12034,false,false,false);s.passkey_valid=code.valid;
 save(d,dir,"real-pair-late-code-cancelled",&s);assert(!s.passkey_valid&&s.p.page==HM_PAGE_MAIN&&!label_contains(lv_screen_active(),"012034"));
}
int main(int argc,char **argv) {
 assert(argc==2);lv_init();
 unsigned han=0;for(uint32_t cp=0x4e00;cp<=0x9fff;cp++) {
  lv_font_glyph_dsc_t glyph;
  if(lv_font_get_glyph_dsc(&hm_font_16,&glyph,cp,0)&&!glyph.is_placeholder)han++;
 }
 assert(han==6763);
 lv_display_t *d=lv_display_create(240,320);
 lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(d,flush); hm_ui_create();
 regression_network(d,argv[1]);regression_muted(d,argv[1]);regression_ptt(d,argv[1]);regression_pair_code(d,argv[1]);
 hm_ui_snapshot_t s={.phase=HM_UI_RECEIVING,.now_ms=7200000,.pair_seconds=42};
 s.p.page=HM_PAGE_MAIN;s.p.theme=HM_THEME_GREEN;s.p.bonded=true;s.p.secure=true;s.p.volume=90;
 s.p.core.connected=true;s.p.core.audio_ready=true;s.p.core.ready=true;s.p.core.battery=86;
 s.p.core.direction=HM_DIRECTION_SPEAKER;s.p.core.talker_active=true;s.p.core.talker_uid=11;
 strcpy(s.p.core.network,"online");strcpy(s.p.core.callsign,"BI8SYN");strcpy(s.p.core.server,"成都 · 中继交流");strcpy(s.p.core.talker_callsign,"BH8ABC");
 strcpy(s.distance,"约 12.3 km");strcpy(s.frequency,"145.500 / 438.500 MHz");strcpy(s.grid,"OM20AB");
 strcpy(s.rig,"泉盛对讲机");strcpy(s.antenna,"玻璃钢天线");strcpy(s.height,"12 m");strcpy(s.age,"接收 25 s");strcpy(s.position,"OM20AB (约)");
 strcpy(s.device_name,"RF Link · 12AB");strcpy(s.firmware_version,"2.0.0");
 const char *calls[]={"BH8ABC","BG8XYZ","BA8DEF","BD8JKL","BI8MNO"};
 s.p.history_count=5;
 for(unsigned i=0;i<5;i++){s.p.history[i].uid=11+i;strcpy(s.p.history[i].callsign,calls[i]);s.p.history[i].received_ms=s.now_ms-i*60000;}
 for(unsigned i=0;i<16;i++){s.spectrum[i]=(i*37+11)%100;s.peaks[i]=s.spectrum[i]+1;}
 save(d,argv[1],"receiving",&s);
 for(unsigned i=0;i<4;i++){s.p.theme=i;char name[40];snprintf(name,sizeof name,"receiving-theme-%u",i);save(d,argv[1],name,&s);}
 s.p.theme=HM_THEME_GREEN;s.phase=HM_UI_TRANSMITTING;s.p.core.direction=HM_DIRECTION_MIC;s.tx_elapsed_s=8;save(d,argv[1],"transmitting",&s);
 s.phase=HM_UI_IDLE;s.p.core.direction=HM_DIRECTION_NONE;save(d,argv[1],"idle-history",&s);
 for(unsigned page=HM_PAGE_MENU;page<=HM_PAGE_DEVICE;page++) {
  s.p.page=page;char name[40];snprintf(name,sizeof name,"page-%u",page);save(d,argv[1],name,&s);
 }
 s.p.page=HM_PAGE_MENU;
 for(unsigned i=0;i<HM_MENU_COUNT;i++){
  s.p.menu_index=i;char name[40];snprintf(name,sizeof name,"menu-scroll-%u",i);save(d,argv[1],name,&s);
  assert(label_contains(lv_screen_active(),i==6?"返回":i==5?"频谱  关闭":"设置"));
 }
 /* v3 matrix: both rendering modes, all pages/themes, long calls and missing location. */
 for(unsigned mode=0;mode<2;mode++)for(unsigned theme=0;theme<HM_THEME_COUNT;theme++) {
  s.p.spectrum_enabled=mode;s.p.theme=theme;s.p.menu_index=5;s.p.core.direction=HM_DIRECTION_SPEAKER;
  for(unsigned page=0;page<=HM_PAGE_DEVICE;page++) {
   s.p.page=page;s.phase=HM_UI_RECEIVING;char name[80];
   snprintf(name,sizeof name,"v3-mode-%u-theme-%u-page-%u",mode,theme,page);save(d,argv[1],name,&s);
  }
  s.p.page=HM_PAGE_MAIN;s.phase=HM_UI_TRANSMITTING;s.p.core.direction=HM_DIRECTION_MIC;
  char name[80];snprintf(name,sizeof name,"v3-mode-%u-theme-%u-tx",mode,theme);save(d,argv[1],name,&s);
  assert(label_contains(lv_screen_active(),"00:08"));
  s.phase=HM_UI_RECEIVING;s.p.core.direction=HM_DIRECTION_SPEAKER;s.distance[0]=0;s.grid[0]=0;
  snprintf(name,sizeof name,"v3-mode-%u-theme-%u-missing",mode,theme);save(d,argv[1],name,&s);
  if(!mode)assert(label_contains(lv_screen_active(),"暂无位置信息"));
  strcpy(s.p.core.talker_callsign,"BI8SYN/PORTABLE");
  snprintf(name,sizeof name,"v3-mode-%u-theme-%u-long",mode,theme);save(d,argv[1],name,&s);
  if(!mode){assert(label_contains(lv_screen_active(),"BI8SYN/"));assert(label_contains(lv_screen_active(),"PORTABLE"));}
  check_full_call(lv_screen_active(),s.p.core.talker_callsign);
  strcpy(s.p.core.talker_callsign,"BH8ABC");strcpy(s.distance,"约 12.3 km");strcpy(s.grid,"OM20AB");
 }
 s.p.spectrum_enabled=false;s.p.theme=HM_THEME_GREEN;
 s.p.notice=HM_NOTICE_SPECTRUM_FAILED;s.p.page=HM_PAGE_MENU;
 save(d,argv[1],"spectrum-save-failed",&s);assert(label_contains(lv_screen_active(),"频谱未保存"));s.p.notice=HM_NOTICE_NONE;
 s.p.page=HM_PAGE_DETAIL;s.p.core.talker_active=false;s.p.detail_page=0;save(d,argv[1],"station-empty",&s);
 assert(label_contains(lv_screen_active(),"暂无通话资料"));
 s.p.core.talker_active=true;s.distance[0]=0;s.position[0]=0;s.grid[0]=0;s.age[0]=0;save(d,argv[1],"station-missing",&s);
 strcpy(s.distance,"约 12.3 km");strcpy(s.position,"OM20AB (约)");strcpy(s.grid,"OM20AB");strcpy(s.age,"接收 25 s");s.p.detail_page=1;save(d,argv[1],"chinese-station",&s);
 assert(label_contains(lv_screen_active(),"泉盛对讲机"));assert(label_contains(lv_screen_active(),"玻璃钢天线"));
 strcpy(s.rig,"泉盛对讲机📡");save(d,argv[1],"unicode-limited",&s);
 assert(label_contains(lv_screen_active(),"泉盛对讲机?"));assert(label_contains(lv_screen_active(),"部分字符以?显示"));
 s.p.page=HM_PAGE_PAIR;
 for(unsigned i=0;i<=HM_PAIR_SAVED;i++){s.p.pair_state=i;s.p.pair_window=i==HM_PAIR_WINDOW;char name[40];snprintf(name,sizeof name,"pair-%u",i);save(d,argv[1],name,&s);}
 s.p.pair_state=HM_PAIR_WINDOW;s.p.pair_window=true;s.passkey_valid=true;s.passkey=12034;save(d,argv[1],"passkey",&s);
 assert(label_contains(lv_screen_active(),"012034"));
 s.p.page=HM_PAGE_VOLUME;s.p.volume=0;save(d,argv[1],"volume-muted",&s);s.p.volume=100;save(d,argv[1],"volume-full",&s);s.p.volume=90;
 s.p.notice=HM_NOTICE_VOLUME_FAILED;s.p.page=HM_PAGE_VOLUME;save(d,argv[1],"volume-save-failed",&s);
 assert(label_contains(lv_screen_active(),"音量未保存"));
 s.p.notice=HM_NOTICE_THEME_FAILED;s.p.page=HM_PAGE_THEME;save(d,argv[1],"theme-save-failed",&s);
 assert(label_contains(lv_screen_active(),"主题未保存"));
 s.p.notice=HM_NOTICE_UNPAIR_FAILED;s.p.page=HM_PAGE_REMOVE_CONFIRM;save(d,argv[1],"remove-failed",&s);
 assert(label_contains(lv_screen_active(),"未能移除手机"));s.p.notice=HM_NOTICE_NONE;
 s.p.page=HM_PAGE_PHONE;
 for(unsigned i=0;i<3;i++){s.p.bonded=i>0;s.p.core.connected=i==2;char name[40];snprintf(name,sizeof name,"phone-%u",i);save(d,argv[1],name,&s);}
 s.p.page=HM_PAGE_MAIN;s.p.core.connected=true;s.p.core.ready=true;s.p.core.battery=-1;s.p.volume=0;
 s.phase=HM_UI_IDLE;save(d,argv[1],"unknown-battery-muted",&s);
 assert(label_contains(lv_screen_active(),"--"));assert(label_contains(lv_screen_active(),"静音"));
 s.p.history_count=0;save(d,argv[1],"empty-history",&s);s.p.page=HM_PAGE_HISTORY;save(d,argv[1],"empty-history-page",&s);s.p.page=HM_PAGE_MAIN;assert(label_contains(lv_screen_active(),"还没有通话记录"));
 s.phase=HM_UI_WAITING;save(d,argv[1],"waiting",&s);
 s.phase=HM_UI_ERROR;strcpy(s.message,"本次讲话已结束 / 松开后可继续");save(d,argv[1],"error",&s);
 s.audio_fault=true;save(d,argv[1],"audio-fault",&s);s.audio_fault=false;
 s.settings_fault=true;save(d,argv[1],"settings-fault",&s);assert(label_contains(lv_screen_active(),"设置暂不可用"));s.settings_fault=false;
 s.p.core.ready=false;save(d,argv[1],"server-offline",&s);s.p.core.ready=true;
 s.p.core.connected=false;s.p.disconnected_ms=s.now_ms;save(d,argv[1],"reconnecting",&s);
 assert(!label_contains(lv_screen_active(),s.p.core.server));assert(!label_contains(lv_screen_active(),s.p.core.callsign));assert(!label_contains(lv_screen_active(),s.p.core.talker_callsign));
 s.now_ms+=3000;save(d,argv[1],"waiting-phone",&s);s.p.core.connected=true;
 s.phase=HM_UI_RECEIVING;s.message[0]=0;s.p.core.direction=HM_DIRECTION_SPEAKER;
 strcpy(s.p.core.callsign,"WWWWWWWWWWWWWWWWWWWWWWWW");strcpy(s.p.core.talker_callsign,"WWWWWWWWWWWWWWWWWWWWWWWW");
 strcpy(s.p.core.server,"成都业余无线电中继 / \"RF Link\" ");strcpy(s.distance,"约 0 m");
 save(d,argv[1],"maximum-values",&s);assert(label_contains(lv_screen_active(),s.p.core.talker_callsign));assert(label_contains(lv_screen_active(),"0 m"));check_full_call(lv_screen_active(),s.p.core.talker_callsign);
 s.p.history_count=5;for(unsigned i=0;i<5;i++)strcpy(s.p.history[i].callsign,"WWWWWWWWWWWWWWWWWWWWWWWW");
 s.p.page=HM_PAGE_HISTORY;save(d,argv[1],"history-long",&s);
 for(unsigned i=0;i<5;i++){s.p.history_page=i;save(d,argv[1],"history-scrolled",&s);assert(label_contains(lv_screen_active(),s.p.history[i].callsign));}
 s.p.page=HM_PAGE_MAIN;
 hm_ui_render(&s);lv_obj_t *ui=lv_obj_get_child(lv_screen_active(),0);unsigned stable=lv_obj_get_child_count(ui);
 assert(stable==58);lv_obj_t *objects[58];for(unsigned i=0;i<stable;i++)objects[i]=lv_obj_get_child(ui,(int)i);
 uint32_t fast_before=hm_ui_fast_render_count();
 for(unsigned frame=0;frame<150;frame++) {
  if(frame==100)s.p.spectrum_enabled=true;
  for(unsigned i=0;i<16;i++){s.spectrum[i]=(frame*13+i*7)%101;s.peaks[i]=s.spectrum[i];}
  s.p.core.next_seq+=5;s.p.core.grant_end+=5;s.p.core.tx_seq+=5;s.p.history[0].received_ms=s.now_ms;
  s.tx_elapsed_s=frame;s.phase=frame<75?HM_UI_RECEIVING:HM_UI_TRANSMITTING;s.now_ms+=50;
  hm_ui_render(&s);lv_obj_update_layout(ui);check_labels(lv_screen_active());
  check_bounds(lv_screen_active());assert(lv_obj_get_child_count(ui)==stable);for(unsigned i=0;i<stable;i++)assert(lv_obj_get_child(ui,(int)i)==objects[i]);check_full_call(lv_screen_active(),s.p.core.talker_callsign);
 }
 assert(hm_ui_fast_render_count()-fast_before>=147);printf("Realistic ADPCM counters/history: fast renders=%u/150\n",hm_ui_fast_render_count()-fast_before);
 s.phase=HM_UI_IDLE;s.p.history_count=1;s.p.history[0].received_ms=s.now_ms;
 save(d,argv[1],"idle-history-before-minute",&s);assert(label_contains(lv_screen_active(),"刚刚"));
 uint32_t idle_full=hm_ui_full_render_count();
 for(unsigned poll=0;poll<20;poll++){s.now_ms+=50;hm_ui_render(&s);}
 assert(hm_ui_full_render_count()==idle_full); /* idle polling does not invalidate layout */
 uint32_t idle_fast=hm_ui_fast_render_count();s.now_ms+=60000;
 save(d,argv[1],"idle-history-after-minute",&s);assert(label_contains(lv_screen_active(),"1分钟前"));assert(hm_ui_fast_render_count()==idle_fast);
 /* Incoming RX counters/history continue while static settings pages are open. */
 const hm_product_page_t static_pages[]={HM_PAGE_MENU,HM_PAGE_THEME,HM_PAGE_DEVICE,HM_PAGE_HISTORY};
 s.phase=HM_UI_RECEIVING;s.p.core.direction=HM_DIRECTION_SPEAKER;s.p.core.talker_active=true;
 strcpy(s.p.core.talker_callsign,"BH8ABC");s.p.history_count=1;strcpy(s.p.history[0].callsign,"BH8ABC");
 for(unsigned mode=0;mode<2;mode++)for(unsigned page=0;page<4;page++) {
  s.p.spectrum_enabled=mode;s.p.page=static_pages[page];s.p.detail_page=0;s.p.notice=HM_NOTICE_NONE;
  s.p.history[0].received_ms=s.now_ms;hm_ui_render(&s);uint32_t count=hm_ui_full_render_count();
  for(unsigned frame=0;frame<100;frame++) {
   s.now_ms+=50;s.p.core.next_seq+=3;s.p.core.grant_end+=3;s.p.core.tx_seq+=3;s.tx_elapsed_s++;
   s.p.history[0].received_ms=s.now_ms;s.spectrum[0]=(frame*7)%101;s.peaks[0]=s.spectrum[0];
   snprintf(s.age,sizeof s.age,"接收 %u s",frame);
   hm_ui_render(&s);assert(hm_ui_full_render_count()==count);
  }
  strcpy(s.p.core.talker_callsign,"BG8XYZ");hm_ui_render(&s);assert(hm_ui_full_render_count()==++count);
  s.p.notice=HM_NOTICE_SPECTRUM_FAILED;hm_ui_render(&s);assert(hm_ui_full_render_count()==++count);
  s.p.notice=HM_NOTICE_NONE;hm_ui_render(&s);assert(hm_ui_full_render_count()==++count);
  strcpy(s.p.core.talker_callsign,"BH8ABC");
 }
 s.p.page=HM_PAGE_HISTORY;s.p.history[0].received_ms=s.now_ms;hm_ui_render(&s);
 uint32_t history_full=hm_ui_full_render_count();s.now_ms+=60000;hm_ui_render(&s);
 assert(hm_ui_full_render_count()==history_full+1);assert(label_contains(lv_screen_active(),"1分钟前"));
 for(unsigned i=0;i<100;i++){s.p.page=i%(HM_PAGE_DEVICE+1);s.p.theme=i%4;hm_ui_render(&s);lv_obj_update_layout(ui);check_labels(lv_screen_active());check_bounds(lv_screen_active());assert(lv_obj_get_child_count(ui)==stable);}
 lv_mem_monitor_t final;lv_mem_monitor(&final);printf("LVGL 40KiB: maxused=%zu free=%zu largest=%zu, objects=%u\n",final.max_used,final.free_size,final.free_biggest_size,stable);
 puts("RF Link real LVGL rendering / all displayed glyphs / all v3 pages and both spectrum modes / four themes / 150 stable-object frames / 100 page transitions: PASS");
}
'''

def run(*args):
    subprocess.run(list(map(str, args)), check=True)

def chunk(kind, data):
    return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))

def main():
    app = ROOT / 'firmware/rf-link-handmic/main'
    candidates = [ROOT / f'firmware/{target}/managed_components/lvgl__lvgl' for target in
                  ('rf-link-handmic', 'ear-trainer', 'resistor-reader', 'ble-signer-lab')]
    lvgl = next((p for p in candidates if (p / 'CMakeLists.txt').is_file()), None)
    if not lvgl:
        raise SystemExit('Run ./tools/dev build rf-link-handmic first to resolve LVGL sources')
    source = Path('/build/preview/rf-link-handmic-source')
    build = Path('/build/preview/rf-link-handmic')
    output = ROOT / 'artifacts/rf-link-handmic-preview'
    source.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    for name, data in [('CMakeLists.txt', CMAKE), ('lv_conf.h', CONF), ('preview.c', HARNESS)]:
        path = source / name
        if not path.exists() or path.read_text() != data:
            path.write_text(data)
    run('cmake', '-S', source, '-B', build, '-G', 'Ninja', f'-DLVGL_DIR={lvgl}', f'-DAPP_DIR={app}', f'-DCJSON_DIR={Path(os.environ.get("IDF_PATH", "/opt/esp/idf")) / "components/json/cJSON"}')
    run('cmake', '--build', build, '--parallel', '8')
    run(build / 'handmic_preview', output)
    for ppm in output.glob('*.ppm'):
        header, size, maximum, pixels = ppm.read_bytes().split(b'\n', 3)
        assert (header, size, maximum, len(pixels)) == (b'P6', b'240 320', b'255', 240 * 320 * 3)
        scanlines = b''.join(b'\0' + pixels[y*720:(y+1)*720] for y in range(320))
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!IIBBBBB', 240, 320, 8, 2, 0, 0, 0))
        png += chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b'')
        ppm.with_suffix('.png').write_bytes(png)
    print(f'Preview PNGs: {output}')

if __name__ == '__main__':
    main()
