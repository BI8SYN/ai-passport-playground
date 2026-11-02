#include "hm_ui.h"
#include <stdio.h>
#include <string.h>

/* Fixed pool: page, phase, spectrum and theme updates never recreate objects.
 * Bars use a three-pixel built-in shadow, no canvas, animation or glow buffer. */
#define LABELS 18
#define RECTS 8
#define BARS 32
static lv_obj_t *root, *labels[LABELS], *rects[RECTS], *bars[BARS];
static bool used_label[LABELS], used_rect[RECTS], used_bar[BARS], missing_glyph;
static const hm_theme_palette_t *palette;
static hm_ui_snapshot_t previous;
static bool previous_valid;
#ifdef HM_UI_TESTING
static uint32_t fast_render_count, full_render_count;
uint32_t hm_ui_full_render_count(void) { return full_render_count; }
uint32_t hm_ui_fast_render_count(void) { return fast_render_count; }
#endif
static lv_style_t label_style, shape_style, bar_style, peak_style;

static void supported(char *out, size_t cap, const char *input)
{
    size_t n=0; const unsigned char *p=(const unsigned char *)(input?input:"");
    while(*p&&n+1<cap) {
        const unsigned char *start=p; uint32_t cp=*p++; unsigned continuation=0;
        if(cp>=0xc2&&cp<=0xdf) { cp&=31; continuation=1; }
        else if(cp>=0xe0&&cp<=0xef) { cp&=15; continuation=2; }
        else if(cp>=0xf0&&cp<=0xf4) { cp&=7; continuation=3; }
        else if(cp>=0x80) { cp='?'; missing_glyph=true; }
        bool valid=true;
        for(unsigned i=0;i<continuation;i++) {
            if((*p&0xc0)!=0x80) { valid=false; break; }
            cp=(cp<<6)|(*p++&63);
        }
        /* A line break is layout, not a missing glyph. */
        lv_font_glyph_dsc_t glyph;
        bool available=valid&&(cp=='\n'||(cp>=32&&lv_font_get_glyph_dsc(&hm_font_16,&glyph,cp,0)&&!glyph.is_placeholder));
        size_t len=(size_t)(p-start);
        if(!available) { out[n++]='?'; missing_glyph=true; }
        else if(n+len<cap) { memcpy(out+n,start,len); n+=len; }
        else break;
    }
    out[n]=0;
}
static void visible(lv_obj_t *obj, bool show)
{
    lv_obj_set_hidden(obj,!show);
}
static bool ascii(const char *s)
{
    for(;*s;s++) if((unsigned char)*s>=128) return false;
    return true;
}
static int text_width(const char *s,const lv_font_t *font)
{
    lv_point_t size; lv_text_get_size(&size,s,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE); return size.x;
}
static const lv_font_t *call_font(const char *s,int width,bool large)
{
    if(!ascii(s)) return &hm_font_16;
    if(large&&text_width(s,&lv_font_montserrat_32)<=width) return &lv_font_montserrat_32;
    if(text_width(s,&hm_font_16)<=width) return &hm_font_16;
    return &lv_font_montserrat_12;
}
static lv_obj_t *label(unsigned i,int x,int y,int w,int h,const char *s,uint32_t color,const lv_font_t *font,bool wrap)
{
    char clean[384]; supported(clean,sizeof clean,s);
    lv_obj_t *o=labels[i]; used_label[i]=true;
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    if(font&&font!=&hm_font_16) lv_obj_set_style_text_font(o,font,0);
    else lv_obj_remove_local_style_prop(o,LV_STYLE_TEXT_FONT,0);
    if(color!=palette->primary) lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    else lv_obj_remove_local_style_prop(o,LV_STYLE_TEXT_COLOR,0);
    lv_obj_remove_local_style_prop(o,LV_STYLE_TEXT_ALIGN,0);
    lv_label_set_long_mode(o,wrap?LV_LABEL_LONG_WRAP:LV_LABEL_LONG_DOT);
    if(strcmp(lv_label_get_text(o),clean)) lv_label_set_text(o,clean);
    visible(o,true);
    return o;
}
static void centered(unsigned i,int y,int h,const char *s,uint32_t color,const lv_font_t *font)
{
    lv_obj_t *o=label(i,12,y,216,h,s,color,font,true);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
}
static lv_obj_t *rect(unsigned i,int x,int y,int w,int h,uint32_t color)
{
    lv_obj_t *o=rects[i];used_rect[i]=true;
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);visible(o,true);return o;
}
static bool server_ready(const hm_snapshot_t *s) { return s->ready&&!strcmp(s->network,"online"); }
static const char *known(const char *s) { return s[0]?s:"--"; }
static const char *notice(const hm_ui_snapshot_t *s)
{
    switch(s->p.notice) {
    case HM_NOTICE_VOLUME_FAILED:return "音量未保存 / 请再试一次";
    case HM_NOTICE_THEME_FAILED:return "主题未保存 / 请再试一次";
    case HM_NOTICE_SPECTRUM_FAILED:return "频谱未保存 / 请再试一次";
    case HM_NOTICE_UNPAIR_FAILED:return "未能移除手机 / 请重试";
    default:return "";
    }
}
static void footer(const hm_ui_snapshot_t *s,const char *hint)
{
    const char *line=missing_glyph?"部分字符以?显示":notice(s)[0]?notice(s):hint;
    /* A message occupies its own bottom strip, never overlays identity or bars. */
    rect(3,12,281,216,1,palette->line);
    label(15,12,290,216,22,line,palette->secondary,NULL,false);
}
static void heading(const hm_ui_snapshot_t *s,const char *title,bool main)
{
    label(0,12,12,156,23,main?"RF Link":title,palette->primary,NULL,false);
    if(main) {
        char battery[16];
        if(s->p.core.battery>=0&&s->p.core.battery<=100) snprintf(battery,sizeof battery,"%d%%",s->p.core.battery);
        else snprintf(battery,sizeof battery,"--");
        label(1,190,13,39,20,battery,palette->secondary,&lv_font_montserrat_12,false);
        lv_obj_t *outline=rect(4,168,15,17,10,palette->background);
        lv_obj_set_style_border_width(outline,1,0);lv_obj_set_style_border_color(outline,lv_color_hex(palette->secondary),0);
        rect(6,185,18,2,4,palette->secondary);
        if(s->p.core.battery>0&&s->p.core.battery<=100) rect(5,170,17,((s->p.core.battery*13+99)/100),6,palette->secondary);
        label(2,12,43,216,23,s->p.core.connected&&server_ready(&s->p.core)&&s->p.core.server[0]?s->p.core.server:"服务器未连接",palette->secondary,NULL,false);
        rect(0,12,72,216,1,palette->line);
    } else {
        const char *sub=s->audio_fault?"声音暂不可用":!s->p.core.connected?"等待手机":!server_ready(&s->p.core)?"请在App中连接服务器":"RF Link";
        char receiving[96];
        if(!s->audio_fault&&s->p.core.connected&&server_ready(&s->p.core)&&s->p.core.direction==HM_DIRECTION_SPEAKER&&s->p.core.talker_active) {
            snprintf(receiving,sizeof receiving,"收听中 · %s",s->p.core.talker_callsign);sub=receiving;
        }
        label(1,12,43,216,23,sub,palette->secondary,NULL,false);
        rect(0,12,72,216,1,palette->line);
    }
}
static void spectrum(const hm_ui_snapshot_t *s,int top,int height,bool sample)
{
    for(unsigned i=0;i<16;i++) {
        unsigned level=sample?(unsigned[]){10,20,36,60,82,65,48,90,71,50,60,35,28,18,12,6}[i]:s->spectrum[i];
        unsigned peak=sample?level:s->peaks[i];if(level>100)level=100;if(peak>100)peak=100;
        int h=(int)(level*(unsigned)height/100);int ph=(int)(peak*(unsigned)height/100);
        if(h) {
            lv_obj_t *o=bars[i];used_bar[i]=true;lv_obj_set_pos(o,14+(int)i*13,top+height-h);lv_obj_set_size(o,8,h);
            lv_obj_remove_local_style_prop(o,LV_STYLE_BG_COLOR,0);
            lv_obj_remove_local_style_prop(o,LV_STYLE_SHADOW_WIDTH,0);visible(o,true);
        }
        if(ph) {
            lv_obj_t *o=bars[16+i];used_bar[16+i]=true;lv_obj_set_pos(o,14+(int)i*13,top+height-ph);lv_obj_set_size(o,8,1);
            lv_obj_remove_local_style_prop(o,LV_STYLE_BG_COLOR,0);lv_obj_remove_local_style_prop(o,LV_STYLE_SHADOW_WIDTH,0);visible(o,true);
        }
    }
}
static void history_rows(const hm_ui_snapshot_t *s,unsigned start,int y,int bottom,unsigned maximum,bool compact)
{
    unsigned slot=compact?8:3,shown=0;
    for(unsigned i=start;i<s->p.history_count&&i<HM_HISTORY_MAX&&shown<maximum;i++) {
        const hm_history_entry_t *h=&s->p.history[i];
        const lv_font_t *font=call_font(h->callsign,compact?145:216,false);
        bool side=text_width(h->callsign,font)<=145;
        bool two=text_width(h->callsign,font)>(side?145:216);
        int height=two?(int)font->line_height*2+2:(int)font->line_height+2;
        int row=side?(compact?height:36):height+22;
        if(y+row>bottom) break;
        label(slot++,12,y,side?145:216,height,h->callsign,palette->primary,font,true);
        char age[40];hm_product_age(h->received_ms,s->now_ms,age,sizeof age);
        label(slot++,side?163:12,side?y:y+height,side?65:216,21,age,palette->secondary,NULL,false);
        y+=row;shown++;
    }
}
static void main_page(const hm_ui_snapshot_t *s)
{
    const hm_snapshot_t *c=&s->p.core;heading(s,"RF Link",true);
    bool rx=s->phase==HM_UI_RECEIVING,tx=s->phase==HM_UI_TRANSMITTING;
    const char *status=rx?"收听中":tx?"讲话中":"守听";
    const char *identity=rx?known(c->talker_callsign):known(c->callsign);
    const char *hint=c->muted?"请在App中取消静音":"按住OK讲话  长按上设置";
    const char *problem=NULL,*action=NULL;
    if(s->settings_fault) { problem="设置暂不可用";action="请重启设备";hint="RF Link"; }
    else if(s->audio_fault) { problem="声音暂不可用";action="请重启设备";hint="长按上查看设置"; }
    else if(!c->connected) {
        bool brief=s->p.bonded&&s->now_ms>=s->p.disconnected_ms&&s->now_ms-s->p.disconnected_ms<2000;
        problem=brief?"正在连接手机":"等待手机";action=brief?"请稍候":"打开手机App\n连接RF Link";hint="长按上查看设置";
    } else if(!server_ready(c)) { problem="暂时无法讲话";action="请在App中连接服务器";hint="长按上查看设置"; }
    else if(s->phase==HM_UI_WAITING) { problem="正在连接通话";action="请稍候";hint="松开OK取消"; }
    else if(s->phase==HM_UI_ERROR) { problem=s->message[0]?s->message:"暂时无法讲话";action=!strcmp(c->stop_reason,"tx_timeout")?"松开后可继续":"松开后重试";hint="请先松开按键"; }
    else if(c->wait_release) { problem="请先松开按键";action="松开后可继续"; }
    if(problem) {
        label(3,12,92,216,58,problem,palette->primary,NULL,true);
        label(4,12,163,216,65,action,palette->secondary,NULL,true);
        footer(s,hint);return;
    }
    if(tx) {
        rect(2,12,85,76,26,palette->primary);label(3,17,89,68,22,status,palette->background,NULL,false);
        char elapsed[24];snprintf(elapsed,sizeof elapsed,"%02lu:%02lu",(unsigned long)(s->tx_elapsed_s/60),(unsigned long)(s->tx_elapsed_s%60));
        if(s->p.spectrum_enabled) label(5,157,88,71,24,elapsed,palette->primary,NULL,false);
    } else label(3,12,88,156,24,status,palette->secondary,NULL,false);
    if(s->p.volume==0||c->muted) label(6,180,63,48,21,"静音",palette->secondary,NULL,false);
    const lv_font_t *font=call_font(identity,216,true);
    bool two=text_width(identity,font)>216;
    char split_call[32];const char *display_identity=identity;
    if(!s->p.spectrum_enabled&&ascii(identity)) {
        lv_point_t size;lv_text_get_size(&size,identity,&lv_font_montserrat_32,0,1,216,LV_TEXT_FLAG_NONE);
        if(size.y<=71) font=&lv_font_montserrat_32;
        /* Preserve a complete slash suffix instead of wrapping inside it. */
        const char *slash=strchr(identity,'/');
        if(slash&&text_width(identity,&lv_font_montserrat_32)>216&&strlen(identity)+1<sizeof split_call) {
            size_t prefix=(size_t)(slash-identity)+1;
            memcpy(split_call,identity,prefix);split_call[prefix]=0;
            if(text_width(split_call,&lv_font_montserrat_32)<=216&&text_width(slash+1,&lv_font_montserrat_32)<=216) {
                split_call[prefix]='\n';strcpy(split_call+prefix+1,slash+1);
                display_identity=split_call;font=&lv_font_montserrat_32;
            }
        }
    }
    label(4,12,s->p.spectrum_enabled?119:(font==&lv_font_montserrat_32&&text_width(identity,font)>216?110:113),216,s->p.spectrum_enabled?54:72,display_identity,palette->primary,font,true);
    if(rx||tx) {
        if(s->p.spectrum_enabled) {
            spectrum(s,two?181:175,two?35:43,false);
            if(rx) {
                rect(1,12,229,216,1,palette->line);
                const char *distance=known(s->distance);
                bool approximate=strncmp(distance,"约",strlen("约"))==0;
                if(approximate) {
                    label(7,12,239,20,30,"约",palette->primary,NULL,false);
                    distance+=strlen("约");while(*distance==' ')distance++;
                }
                label(8,approximate?35:12,236,approximate?135:158,35,distance,palette->primary,ascii(distance)?call_font(distance,135,true):NULL,false);
                label(9,174,245,54,22,known(s->grid),palette->secondary,&lv_font_montserrat_12,false);
            } else label(7,12,244,216,23,"松开 OK 结束",palette->secondary,NULL,false);
        } else {
            rect(1,12,183,216,1,palette->line);
            if(rx) {
                bool location=s->distance[0]||s->grid[0];
                label(7,12,194,152,23,location?"距离":"电台位置",palette->secondary,NULL,false);
                if(location) {
                    label(9,174,194,54,23,"网格",palette->secondary,NULL,false);
                    const char *distance=known(s->distance);
                    bool approximate=strncmp(distance,"约",strlen("约"))==0;
                    if(approximate) {
                        label(10,12,228,20,25,"约",palette->secondary,NULL,false);
                        distance+=strlen("约");while(*distance==' ')distance++;
                    }
                    label(8,approximate?35:12,220,approximate?133:156,43,distance,palette->primary,call_font(distance,133,true),false);
                    label(11,174,232,54,23,known(s->grid),palette->secondary,&lv_font_montserrat_12,false);
                } else label(8,12,225,216,32,"暂无位置信息",palette->secondary,NULL,false);
            } else {
                label(7,12,194,216,23,"本次讲话",palette->secondary,NULL,false);
                char elapsed[24];snprintf(elapsed,sizeof elapsed,"%02lu:%02lu",(unsigned long)(s->tx_elapsed_s/60),(unsigned long)(s->tx_elapsed_s%60));
                label(5,12,218,216,42,elapsed,palette->primary,&lv_font_montserrat_32,false);
                label(8,12,258,216,22,"松开 OK 结束",palette->secondary,NULL,false);
            }
        }
        const hm_history_entry_t *prior=hm_product_previous(&s->p);
        footer(s,"");
        if(prior&&!missing_glyph&&!notice(s)[0]) {
            const lv_font_t *small=call_font(prior->callsign,163,false);
            if(text_width(prior->callsign,small)<=163) {
                label(14,12,290,48,22,"上一位",palette->secondary,NULL,false);
                label(15,65,290,163,22,prior->callsign,palette->secondary,small,false);
            }
        }
    } else {
        rect(1,12,181,216,1,palette->line);
        label(7,12,191,216,23,"最近通话",palette->secondary,NULL,false);
        if(s->p.history_count) history_rows(s,0,212,279,3,true);
        else label(8,12,232,216,24,"还没有通话记录",palette->secondary,NULL,false);
        footer(s,hint);
    }
    if(s->p.volume_overlay_until_ms>s->now_ms) {
        char v[40];snprintf(v,sizeof v,"音量 %u%s",s->p.volume,s->p.volume==0?" 静音":"");
        rect(7,126,85,102,27,palette->primary);label(16,132,89,92,22,v,palette->background,NULL,false);
    } else if(s->message[0]) footer(s,s->message);
}
static void menu_page(const hm_ui_snapshot_t *s)
{
    heading(s,"设置",false);
    static const char *items[]={"最近通话","电台资料","音量","手机连接","主题","频谱","返回"};
    unsigned selected=s->p.menu_index<HM_MENU_COUNT?s->p.menu_index:HM_MENU_COUNT-1;
    unsigned start=selected>=5?selected-4:0;
    rect(2,12,86+(int)(selected-start)*37,216,33,palette->primary);
    for(unsigned row=0;row<5&&start+row<HM_MENU_COUNT;row++) {
        unsigned i=start+row;char item[64];
        snprintf(item,sizeof item,"%s%s",items[i],i==5?(s->p.spectrum_enabled?"  开启":"  关闭"):"");
        label(3+row,22,92+(int)row*37,194,23,item,i==selected?palette->background:palette->primary,NULL,false);
    }
    footer(s,"上下选择  OK确认");
}
static void history_page(const hm_ui_snapshot_t *s)
{
    heading(s,"最近通话",false);
    if(!s->p.history_count) centered(3,147,40,"还没有通话记录",palette->secondary,NULL);
    else history_rows(s,s->p.history_page<s->p.history_count?s->p.history_page:0,86,278,5,false);
    footer(s,"上下翻页  OK回主屏");
}
static void field(unsigned slot,int y,const char *name,const char *value,int h)
{
    label(slot,12,y,216,21,name,palette->secondary,NULL,false);
    label(slot+1,12,y+23,216,h,known(value),palette->primary,call_font(known(value),216,false),true);
}
static void detail_page(const hm_ui_snapshot_t *s)
{
    heading(s,s->p.detail_page?"电台资料 2/2":"电台资料 1/2",false);
    if(!s->p.core.talker_active) { centered(3,146,42,"暂无通话资料",palette->secondary,NULL);footer(s,"上下翻页  OK回主屏");return; }
    if(!s->p.detail_page) {
        label(3,12,83,216,32,known(s->p.core.talker_callsign),palette->primary,call_font(s->p.core.talker_callsign,216,false),true);
        field(4,119,"服务器",s->p.core.server,44);
        char location[160];snprintf(location,sizeof location,"%s  %s",known(s->grid),known(s->position));
        field(6,186,"网格 / 位置",location,44);
        char distance[128];snprintf(distance,sizeof distance,"距离 %s  %s",known(s->distance),known(s->age));
        label(8,12,257,216,22,distance,palette->secondary,NULL,false);
    } else {
        field(3,82,"频率",s->frequency,44);
        field(5,151,"型号 / 天线",s->rig,24);
        label(7,12,199,216,24,known(s->antenna),palette->primary,NULL,true);
        field(8,227,"天线高度",s->height,27);
    }
    footer(s,"上下翻页  OK回主屏");
}
static void volume_page(const hm_ui_snapshot_t *s)
{
    heading(s,"音量",false);char value[12];snprintf(value,sizeof value,"%u",s->p.volume);
    centered(3,105,44,value,palette->primary,&lv_font_montserrat_32);
    centered(4,154,25,s->p.volume?"实时调整":"静音",palette->secondary,NULL);
    for(unsigned i=0;i<20;i++) {
        lv_obj_t *o=bars[i];used_bar[i]=true;lv_obj_set_pos(o,12+(int)i*11,201);lv_obj_set_size(o,8,31);
        lv_obj_set_style_bg_color(o,lv_color_hex(i*5<s->p.volume?palette->primary:palette->line),0);
        lv_obj_set_style_shadow_width(o,0,0);visible(o,true);
    }
    centered(5,251,24,"上下调整  长按OK取消",palette->secondary,NULL);
    footer(s,"OK保存并返回");
}
static void phone_page(const hm_ui_snapshot_t *s)
{
    heading(s,"手机连接",false);
    label(3,12,87,216,25,s->p.core.connected?"手机已连接":s->p.bonded?"等待手机":"尚未连接手机",palette->primary,NULL,false);
    label(4,12,119,216,44,s->p.bonded?(s->p.core.connected?known(s->device_name):"靠近手机并打开App"):known(s->device_name),palette->secondary,NULL,true);
    const char *bonded[]={"更换手机","移除手机","设备信息","返回"};
    const char *unbonded[]={"连接手机","设备信息","返回"};
    unsigned count=s->p.bonded?4:3,sel=s->p.phone_index<count?s->p.phone_index:count-1;
    rect(2,12,166+(int)sel*27,216,25,palette->primary);
    for(unsigned i=0;i<count;i++)label(5+i,22,169+(int)i*27,194,22,s->p.bonded?bonded[i]:unbonded[i],i==sel?palette->background:palette->primary,NULL,false);
    footer(s,"上下选择  OK确认");
}
static void pair_page(const hm_ui_snapshot_t *s)
{
    heading(s,"连接手机",false);const char *title="在App中添加设备",*sub="按OK开始连接",*hint="OK开始  下键返回";
    switch(s->p.pair_state) {
    case HM_PAIR_WINDOW:title=s->passkey_valid?"请在手机输入":"等待手机";sub="在App中添加设备";hint="下键取消";break;
    case HM_PAIR_CONNECTING:title="正在连接";sub="请稍候";hint="下键取消";break;
    case HM_PAIR_TIMEOUT:title="未连接到手机";sub="OK重试";hint="OK重试  下键返回";break;
    case HM_PAIR_FAILED:title="未能连接";sub="请重试";hint="OK重试  下键返回";break;
    case HM_PAIR_SAVED:title="已保存手机";sub="等待连接";hint="下键返回";break;
    default:break;
    }
    centered(3,89,43,title,palette->primary,NULL);
    centered(4,133,42,known(s->device_name),palette->secondary,NULL);
    if(s->p.pair_window&&s->passkey_valid) {
        char key[12];snprintf(key,sizeof key,"%06lu",(unsigned long)(s->passkey%1000000));
        centered(5,183,42,key,palette->primary,&lv_font_montserrat_32);
    } else centered(5,185,43,sub,palette->secondary,NULL);
    if(s->p.pair_window) { char sec[40];snprintf(sec,sizeof sec,"剩余 %u 秒",s->pair_seconds);centered(6,245,23,sec,palette->secondary,NULL); }
    if(s->message[0]) centered(7,228,49,s->message,palette->secondary,NULL);
    footer(s,hint);
}
static void confirm_page(const hm_ui_snapshot_t *s)
{
    bool replace=s->p.page==HM_PAGE_REPLACE_CONFIRM;heading(s,"手机连接",false);
    centered(3,92,28,replace?"更换手机？":"移除手机？",palette->primary,NULL);
    centered(4,136,50,replace?"原手机将断开":"下次使用需要重新连接",palette->secondary,NULL);
    unsigned sel=s->p.confirm_index?1:0;rect(2,12,204+(int)sel*35,216,31,palette->primary);
    centered(5,211,22,replace?"继续":"移除",sel==0?palette->background:palette->primary,NULL);
    centered(6,246,22,"取消",sel==1?palette->background:palette->primary,NULL);
    footer(s,"上下选择  OK确认");
}
static void theme_page(const hm_ui_snapshot_t *s)
{
    heading(s,"主题",false);centered(3,86,26,palette->name,palette->primary,NULL);
    label(4,12,127,216,43,"BI8SYN",palette->primary,&lv_font_montserrat_32,false);
    if(s->p.spectrum_enabled) spectrum(s,184,45,true);
    else { rect(1,12,183,216,1,palette->line);label(6,12,201,216,24,"最近通话",palette->secondary,NULL,false); }
    label(5,12,245,216,24,"守听 · 最近通话",palette->secondary,NULL,false);
    footer(s,"上下预览  OK保存");
}
static void device_page(const hm_ui_snapshot_t *s)
{
    heading(s,s->p.detail_page?"设备信息 2/2":"设备信息 1/2",false);
    if(!s->p.detail_page) {
        field(3,87,"设备名称",s->device_name,44);
        field(5,166,"固件版本",s->firmware_version,41);
        label(7,12,246,216,24,s->p.core.connected?"手机已连接":s->p.bonded?"已保存手机 / 等待连接":"尚未连接手机",palette->secondary,NULL,false);
    } else field(3,87,"当前服务器",s->p.core.server,132);
    footer(s,"上下翻页  OK返回");
}
void hm_ui_set_phase(hm_ui_snapshot_t *s)
{
    if(!s)return;
    const hm_snapshot_t *c=&s->p.core;
    if(c->direction==HM_DIRECTION_MIC)s->phase=HM_UI_TRANSMITTING;
    else if(c->direction==HM_DIRECTION_SPEAKER)s->phase=HM_UI_RECEIVING;
    else if(c->pending_ptt)s->phase=HM_UI_WAITING;
    else if(s->audio_fault)s->phase=HM_UI_ERROR;
    else s->phase=HM_UI_IDLE;
    if(c->physical_ptt&&c->direction==HM_DIRECTION_NONE&&(c->rejected||c->wait_release)&&c->stop_reason[0]) {
        s->phase=HM_UI_ERROR;
        const char *text=!strcmp(c->stop_reason,"busy")?"有人正在讲话":
                         !strcmp(c->stop_reason,"tx_timeout")?"本次讲话已结束":"暂时无法讲话";
        snprintf(s->message,sizeof s->message,"%s",text);
    }
}
void hm_ui_create(void)
{
    if(root)return;
    root=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(root);lv_obj_set_size(root,240,320);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_set_scrollbar_mode(root,LV_SCROLLBAR_MODE_OFF);lv_obj_set_scroll_dir(root,LV_DIR_NONE);
    lv_style_init(&label_style);lv_style_set_text_font(&label_style,&hm_font_16);lv_style_set_text_line_space(&label_style,1);
    lv_style_init(&shape_style);lv_style_set_bg_opa(&shape_style,LV_OPA_COVER);
    lv_style_init(&bar_style);lv_style_set_bg_opa(&bar_style,LV_OPA_COVER);lv_style_set_radius(&bar_style,1);
    lv_style_set_shadow_width(&bar_style,3);lv_style_set_shadow_opa(&bar_style,LV_OPA_20);
    lv_style_init(&peak_style);lv_style_set_bg_opa(&peak_style,LV_OPA_COVER);
    /* Shared style properties keep the complete fixed pool within the bounded LVGL heap. */
    for(unsigned i=0;i<RECTS;i++) { rects[i]=lv_obj_create(root);lv_obj_remove_style_all(rects[i]);lv_obj_add_style(rects[i],&shape_style,0);visible(rects[i],false); }
    for(unsigned i=0;i<BARS;i++) { bars[i]=lv_obj_create(root);lv_obj_remove_style_all(bars[i]);lv_obj_add_style(bars[i],i<16?&bar_style:&peak_style,0);visible(bars[i],false); }
    for(unsigned i=0;i<LABELS;i++) { labels[i]=lv_label_create(root);lv_obj_remove_style_all(labels[i]);lv_obj_add_style(labels[i],&label_style,0);visible(labels[i],false); }
}
/* Compare what a static page displays, rather than the transport model copied
 * beside it. Incoming audio advances counters, metadata leases and the current
 * history timestamp every frame even while these pages are unchanged. */
static bool static_page_equal(const hm_ui_snapshot_t *a,const hm_ui_snapshot_t *b)
{
    if(a->p.page==HM_PAGE_MAIN||a->p.page!=b->p.page||a->settings_fault||b->settings_fault||
       a->p.theme!=b->p.theme||a->p.notice!=b->p.notice||a->audio_fault!=b->audio_fault) return false;
    bool online_a=a->p.core.connected&&server_ready(&a->p.core);
    bool online_b=b->p.core.connected&&server_ready(&b->p.core);
    if(a->p.core.connected!=b->p.core.connected||online_a!=online_b) return false;
    if(!a->audio_fault&&online_a) {
        bool rx_a=a->p.core.direction==HM_DIRECTION_SPEAKER&&a->p.core.talker_active;
        bool rx_b=b->p.core.direction==HM_DIRECTION_SPEAKER&&b->p.core.talker_active;
        if(rx_a!=rx_b||(rx_a&&strcmp(a->p.core.talker_callsign,b->p.core.talker_callsign))) return false;
    }
#define SAME(field) (!strcmp(a->field,b->field))
    switch(a->p.page) {
    case HM_PAGE_MENU:return a->p.menu_index==b->p.menu_index&&a->p.spectrum_enabled==b->p.spectrum_enabled;
    case HM_PAGE_THEME:return a->p.spectrum_enabled==b->p.spectrum_enabled;
    case HM_PAGE_VOLUME:return a->p.volume==b->p.volume;
    case HM_PAGE_PHONE:return a->p.bonded==b->p.bonded&&a->p.phone_index==b->p.phone_index&&SAME(device_name);
    case HM_PAGE_REPLACE_CONFIRM:case HM_PAGE_REMOVE_CONFIRM:return a->p.confirm_index==b->p.confirm_index;
    case HM_PAGE_PAIR:
        return a->p.pair_state==b->p.pair_state&&a->p.pair_window==b->p.pair_window&&
            a->pair_seconds==b->pair_seconds&&a->passkey_valid==b->passkey_valid&&
            (!a->passkey_valid||a->passkey==b->passkey)&&SAME(device_name)&&SAME(message);
    case HM_PAGE_DEVICE:
        return a->p.detail_page==b->p.detail_page&&
            (a->p.detail_page?SAME(p.core.server):
             (a->p.bonded==b->p.bonded&&SAME(device_name)&&SAME(firmware_version)));
    case HM_PAGE_DETAIL:
        if(a->p.detail_page!=b->p.detail_page||a->p.core.talker_active!=b->p.core.talker_active) return false;
        if(!a->p.core.talker_active)return true;
        return a->p.detail_page?(SAME(frequency)&&SAME(rig)&&SAME(antenna)&&SAME(height)):
            (SAME(p.core.talker_callsign)&&SAME(p.core.server)&&SAME(grid)&&SAME(position)&&SAME(distance)&&SAME(age));
    case HM_PAGE_HISTORY:
        if(a->p.history_count!=b->p.history_count||a->p.history_page!=b->p.history_page)return false;
        for(unsigned i=a->p.history_page<a->p.history_count?a->p.history_page:0;i<a->p.history_count&&i<HM_HISTORY_MAX;i++) {
            if(strcmp(a->p.history[i].callsign,b->p.history[i].callsign)) return false;
            char age_a[40],age_b[40];
            hm_product_age(a->p.history[i].received_ms,a->now_ms,age_a,sizeof age_a);
            hm_product_age(b->p.history[i].received_ms,b->now_ms,age_b,sizeof age_b);
            if(strcmp(age_a,age_b))return false;
        }
        return true;
    default:return false;
    }
#undef SAME
}
void hm_ui_render(const hm_ui_snapshot_t *s)
{
    if(!s)return;
    if(!root)hm_ui_create();
    if(previous_valid&&!memcmp(&previous,s,sizeof *s))return;
    if(previous_valid&&static_page_equal(s,&previous)) { previous=*s;return; }
    if(previous_valid) {
        /* Polling time alone has no visual effect between age/overlay/reconnect
         * boundaries. Keep idle and static pages free of layout invalidations. */
        hm_ui_snapshot_t unchanged=*s;unchanged.now_ms=previous.now_ms;
        bool same_age=true;
        for(unsigned i=0;i<s->p.history_count&&i<HM_HISTORY_MAX;i++) {
            char before[40],after[40];
            hm_product_age(s->p.history[i].received_ms,previous.now_ms,before,sizeof before);
            hm_product_age(s->p.history[i].received_ms,s->now_ms,after,sizeof after);
            if(strcmp(before,after)) same_age=false;
        }
        bool overlay_before=previous.now_ms<s->p.volume_overlay_until_ms;
        bool overlay_after=s->now_ms<s->p.volume_overlay_until_ms;
        bool brief_before=previous.now_ms>=s->p.disconnected_ms&&previous.now_ms-s->p.disconnected_ms<2000;
        bool brief_after=s->now_ms>=s->p.disconnected_ms&&s->now_ms-s->p.disconnected_ms<2000;
        if(same_age&&overlay_before==overlay_after&&(s->p.core.connected||brief_before==brief_after)&&
           !memcmp(&unchanged,&previous,sizeof unchanged)) { previous=*s;return; }
    }
    /* The common 20 Hz path only touches changed bars and the TX clock.
     * Text, headers, history and root background remain untouched. */
    if(previous_valid&&s->p.page==HM_PAGE_MAIN&&
       (s->phase==HM_UI_RECEIVING||s->phase==HM_UI_TRANSMITTING)&&
       !s->settings_fault&&!s->audio_fault&&s->p.core.connected&&server_ready(&s->p.core)&&!s->p.core.wait_release&&
       !(previous.now_ms<s->p.volume_overlay_until_ms&&s->now_ms>=s->p.volume_overlay_until_ms)) {
        hm_ui_snapshot_t stable=*s;
        stable.now_ms=previous.now_ms;stable.tx_elapsed_s=previous.tx_elapsed_s;
        memcpy(stable.spectrum,previous.spectrum,sizeof stable.spectrum);
        memcpy(stable.peaks,previous.peaks,sizeof stable.peaks);
        /* These counters never appear on an active-call screen. Current RX
         * history refreshes at PCM cadence; its timestamps are shown only on
         * the history/idle pages, which do not use this fast path. */
        stable.p.core.next_seq=previous.p.core.next_seq;
        stable.p.core.grant_end=previous.p.core.grant_end;
        stable.p.core.tx_seq=previous.p.core.tx_seq;
        stable.p.core.tele=previous.p.core.tele;
        stable.p.core.rig=previous.p.core.rig;
        stable.p.core.position=previous.p.core.position;
        for(unsigned i=0;i<HM_HISTORY_MAX;i++)stable.p.history[i].received_ms=previous.p.history[i].received_ms;
        memcpy(stable.position,previous.position,sizeof stable.position);
        memcpy(stable.frequency,previous.frequency,sizeof stable.frequency);
        memcpy(stable.rig,previous.rig,sizeof stable.rig);
        memcpy(stable.antenna,previous.antenna,sizeof stable.antenna);
        memcpy(stable.height,previous.height,sizeof stable.height);
        memcpy(stable.age,previous.age,sizeof stable.age);
        if(!memcmp(&stable,&previous,sizeof stable)) {
            memset(used_bar,0,sizeof used_bar);
            const char *call=s->phase==HM_UI_RECEIVING?known(s->p.core.talker_callsign):known(s->p.core.callsign);
            bool two=text_width(call,call_font(call,216,true))>216;
            if(s->p.spectrum_enabled&&
               (memcmp(s->spectrum,previous.spectrum,sizeof s->spectrum)||memcmp(s->peaks,previous.peaks,sizeof s->peaks))) {
                spectrum(s,two?181:175,two?35:43,false);
                for(unsigned i=0;i<BARS;i++)if(!used_bar[i])visible(bars[i],false);
            }
            if(s->phase==HM_UI_TRANSMITTING&&s->tx_elapsed_s!=previous.tx_elapsed_s) {
                char elapsed[24];snprintf(elapsed,sizeof elapsed,"%02lu:%02lu",(unsigned long)(s->tx_elapsed_s/60),(unsigned long)(s->tx_elapsed_s%60));
                if(s->p.spectrum_enabled) label(5,157,88,71,24,elapsed,palette->primary,NULL,false);
                else label(5,12,218,216,42,elapsed,palette->primary,&lv_font_montserrat_32,false);
            }
#ifdef HM_UI_TESTING
            fast_render_count++;
#endif
            previous=*s;return;
        }
    }
#ifdef HM_UI_TESTING
    full_render_count++;
#endif
    bool theme_changed=!previous_valid||previous.p.theme!=s->p.theme;
    previous=*s;previous_valid=true;palette=hm_product_palette(s->p.theme);missing_glyph=false;
    if(theme_changed) {
        lv_style_set_text_color(&label_style,lv_color_hex(palette->primary));
        lv_style_set_bg_color(&bar_style,lv_color_hex(palette->primary));
        lv_style_set_shadow_color(&bar_style,lv_color_hex(palette->primary));
        lv_style_set_bg_color(&peak_style,lv_color_hex(palette->secondary));
        lv_obj_report_style_change(&label_style);lv_obj_report_style_change(&bar_style);lv_obj_report_style_change(&peak_style);
    }
    memset(used_label,0,sizeof used_label);memset(used_rect,0,sizeof used_rect);memset(used_bar,0,sizeof used_bar);
    lv_obj_set_style_bg_color(root,lv_color_hex(palette->background),0);
    if(s->settings_fault) main_page(s);
    else switch(s->p.page) {
    case HM_PAGE_MENU:menu_page(s);break;
    case HM_PAGE_HISTORY:history_page(s);break;
    case HM_PAGE_DETAIL:detail_page(s);break;
    case HM_PAGE_VOLUME:volume_page(s);break;
    case HM_PAGE_PHONE:phone_page(s);break;
    case HM_PAGE_PAIR:pair_page(s);break;
    case HM_PAGE_REPLACE_CONFIRM:case HM_PAGE_REMOVE_CONFIRM:confirm_page(s);break;
    case HM_PAGE_THEME:theme_page(s);break;
    case HM_PAGE_DEVICE:device_page(s);break;
    default:main_page(s);break;
    }
    for(unsigned i=0;i<LABELS;i++)if(!used_label[i])visible(labels[i],false);
    for(unsigned i=0;i<RECTS;i++)if(!used_rect[i])visible(rects[i],false);
    for(unsigned i=0;i<BARS;i++)if(!used_bar[i])visible(bars[i],false);
}
