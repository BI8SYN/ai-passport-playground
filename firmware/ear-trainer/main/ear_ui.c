#include "ear_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static lv_obj_t *volume_panel,*volume_label,*volume_fill;
static lv_obj_t *title,*battery,*caption,*hero,*detail,*footer,*rows[6],*labels[6],*dots[8],*staff[5];
static const uint32_t BG=0x101c25,INK=0xf3efe3,MUTED=0x899ba4,ACCENT=0x9ce3c5,PANEL=0x1b2c37;
static lv_obj_t *text(lv_obj_t *parent,int x,int y,int w,uint32_t color){lv_obj_t *o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_font(o,&ear_font_16,0);lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);return o;}
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius){lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_radius(o,radius,0);return o;}
void et_ui_create(void){
    lv_obj_t *root=lv_screen_active();lv_obj_remove_style_all(root);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_set_style_bg_color(root,lv_color_hex(BG),0);
    title=text(root,20,17,158,INK);battery=text(root,181,17,45,MUTED);
    caption=text(root,20,48,200,MUTED);hero=text(root,20,77,204,INK);lv_obj_set_style_text_font(hero,&lv_font_montserrat_32,0);
    detail=text(root,20,121,202,MUTED);
    for(int i=0;i<5;i++)staff[i]=box(root,22,82+i*11,194,1,0x344753,0);
    for(int i=0;i<8;i++)dots[i]=box(root,30+i*24,95,9,9,ACCENT,5);
    for(int i=0;i<6;i++){rows[i]=box(root,14,66+i*36,212,32,PANEL,7);labels[i]=text(rows[i],10,7,193,INK);}
    footer=text(root,20,294,204,MUTED);
    volume_panel=box(root,18,231,204,54,0x304653,10);
    volume_label=text(volume_panel,12,8,180,INK);
    box(volume_panel,12,35,180,5,BG,2);
    volume_fill=box(volume_panel,12,35,100,5,ACCENT,2);
    lv_obj_set_hidden(volume_panel,true);
}
static void row(int i,int y,const char *value,bool selected,bool disabled){
    lv_obj_set_hidden(rows[i],false);lv_obj_set_y(rows[i],y);lv_label_set_text(labels[i],value);
    lv_obj_set_style_bg_color(rows[i],lv_color_hex(selected?ACCENT:PANEL),0);
    lv_obj_set_style_text_color(labels[i],lv_color_hex(disabled?MUTED:selected?BG:INK),0);
}
static void show_text(lv_obj_t *o,const char *value){lv_obj_set_hidden(o,false);lv_label_set_text(o,value);}
void et_ui_refresh(const et_app_t *a,int64_t now){
    char buf[120];lv_obj_set_y(detail,121);for(int i=0;i<6;i++)lv_obj_set_hidden(rows[i],true);
    for(int i=0;i<8;i++)lv_obj_set_hidden(dots[i],true);
    for(int i=0;i<5;i++)lv_obj_set_hidden(staff[i],true);
    lv_obj_set_hidden(hero,true);lv_obj_set_hidden(detail,true);
    lv_label_set_text(title,a->page==ET_HOME?"听见音乐":et_mode_name(a->config.mode));
    if(a->battery>=0)snprintf(buf,sizeof(buf),"%d%%",a->battery);else snprintf(buf,sizeof(buf),"--");lv_label_set_text(battery,buf);
    lv_label_set_text(footer,"上下选择  确定进入");
    if(a->page==ET_HOME){
        lv_label_set_text(caption,"EAR LAB   /   离线练耳");lv_label_set_text(footer,"长按上下调音量");
        for(int i=0;i<5;i++){snprintf(buf,sizeof(buf),"0%d    %s",i+1,et_mode_name(i));row(i,77+i*40,buf,a->focus==i,false);}
    }else if(a->page==ET_SETUP){
        lv_label_set_text(caption,"训练设置");int first=a->focus>4?a->focus-4:0;
        for(int i=0;i<6&&first+i<et_setting_count();i++){et_setting_text(a,first+i,buf,sizeof(buf));row(i,68+i*36,buf,a->focus==first+i,false);}
        lv_label_set_text(footer,"确定更改  长按返回");
    }else if(a->page==ET_POOL){
        lv_label_set_text(caption,"至少选择两项");int first=a->focus>4?a->focus-4:0;
        for(int i=0;i<6&&first+i<=et_pool_size(a->config.mode,a->config.variant);i++){
            int j=first+i;if(!j)snprintf(buf,sizeof(buf),"完成选择");else snprintf(buf,sizeof(buf),"%s  %s",a->config.custom&(1u<<(j-1))?"+":"-",et_option_name(a->config.mode,a->config.variant,j-1));
            row(i,68+i*36,buf,a->focus==j,false);
        }lv_label_set_text(footer,"确定切换  长按返回");
    }else if(a->page==ET_STATS||a->page==ET_SUMMARY){
        bool summary=a->page==ET_SUMMARY;const et_result_t *r=summary?&a->session:&a->history[a->config.mode*3+a->config.variant];
        lv_label_set_text(caption,summary?"本轮成绩":"累计成绩");snprintf(buf,sizeof(buf),"%lu%%",r->total?(unsigned long)(r->correct*100/r->total):0);show_text(hero,buf);
        snprintf(buf,sizeof(buf),"首答 %lu%%   共 %lu 项",r->total?(unsigned long)(r->first*100/r->total):0,(unsigned long)r->total);show_text(detail,buf);
        int weak=0;for(int i=1;i<ET_OPTIONS;i++)if(r->errors[i]>r->errors[weak])weak=i;
        snprintf(buf,sizeof(buf),r->errors[weak]?"待加强：%s":"继续保持，听得更准",et_option_name(a->config.mode,a->config.variant,weak));row(0,164,buf,false,false);
        row(1,213,summary?"再练一轮":"返回设置",a->focus==0,false);if(summary)row(2,251,"返回首页",a->focus==1,false);
        lv_label_set_text(footer,"确定选择  长按返回");
    }else{
        if(a->config.questions)snprintf(buf,sizeof(buf),"第 %d / %d 题",a->round,a->config.questions);else snprintf(buf,sizeof(buf),"第 %d 题 / 不限",a->round);lv_label_set_text(caption,buf);
        if(a->config.mode==ET_RHYTHM){
            bool ready=a->rhythm_at && now>=a->rhythm_at;
            snprintf(buf,sizeof(buf),a->rhythm_done?"%d%%":"%d / 8",a->rhythm_done?a->rhythm_percent:a->taps);show_text(hero,buf);
            show_text(detail,a->rhythm_done?"逐拍偏差：早 / 晚":ready?"静音跟拍：按确定键":"先听旋律，再听四拍预备");
            for(int i=0;i<8;i++){lv_obj_set_hidden(dots[i],false);lv_obj_set_x(dots[i],27+i*26);int error=a->errors[i];if(error>200)error=200;if(error< -200)error=-200;lv_obj_set_y(dots[i],a->rhythm_done?202+error/5:202);lv_obj_set_style_bg_color(dots[i],lv_color_hex(i<a->taps?ACCENT:0x344753),0);}
            for(int i=0;i<3;i++){lv_obj_set_hidden(staff[i],false);lv_obj_set_y(staff[i],162+i*40);}
            row(0,252,a->rhythm_done?"确定：继续下一题":"聆听 > 预备 > 静音",false,false);lv_label_set_text(footer,"上：重听  下：结束");
        }else{
            if(a->question.steps>1){snprintf(buf,sizeof(buf),"和弦进行  %d / 4",a->question.step+1);show_text(detail,buf);}else show_text(detail,a->verdict==ET_RETRY?"还可再答一次":a->question.done?"听一遍，再记住它":"聆听，然后选择答案");
            if(a->question.done){show_text(hero,a->verdict==ET_CORRECT?"YES":"LISTEN");snprintf(buf,sizeof(buf),"答案：%s",et_option_name(a->config.mode,a->config.variant,a->question.ids[a->question.answer]));row(0,163,buf,false,false);row(1,200,"下一项",a->focus==0,false);row(2,234,"重听题目",a->focus==1,false);
                if(a->config.mode==ET_CHORD)row(3,265,"分解试听",a->focus==2,false);
                lv_obj_set_hidden(hero,true);lv_obj_set_hidden(detail,true);
                for(int k=0;k<5;k++){lv_obj_set_hidden(staff[k],false);lv_obj_set_y(staff[k],80+k*11);}
                int index=0;
                for(int j=0;j<a->question.score.count && index<8;j++){
                    if(a->question.steps>1 && j!=a->question.step)continue;
                    const et_note_t *n=&a->question.score.events[j];
                    for(int k=0;k<n->voices && index<8;k++,index++){
                        static const int degrees[]={0,0,1,1,2,3,3,4,4,5,5,6};int pitch=n->notes[k];int step=(pitch/12-5)*7+degrees[pitch%12];
                        int y=136-step*11/2;if(y<69)y=69;
                        lv_obj_set_hidden(dots[index],false);lv_obj_set_pos(dots[index],30+index*23,y);
                        lv_obj_set_style_bg_color(dots[index],lv_color_hex(ACCENT),0);
                    }
                }
                char pitches[64]="";int used=0;
                for(int j=0;j<a->question.score.count && used<4;j++){
                    if(a->question.steps>1 && j!=a->question.step)continue;
                    const et_note_t *n=&a->question.score.events[j];
                    for(int k=0;k<n->voices && used<4;k++,used++){
                        char name[12];et_note_name(n->notes[k],name,sizeof(name));
                        snprintf(pitches+strlen(pitches),sizeof(pitches)-strlen(pitches),"%s%s",used?"  ":"",name);
                    }
                }
                lv_obj_set_y(detail,146);show_text(detail,pitches);
                snprintf(buf,sizeof(buf),"%s  /  第 %d 题",a->verdict==ET_CORRECT?"答对了":"本题结束",a->round);lv_label_set_text(caption,buf);
                lv_obj_set_style_text_color(labels[0],lv_color_hex(a->verdict==ET_CORRECT?ACCENT:0xffc295),0);
            }
            else{
                show_text(hero,"LISTEN");int count=a->question.count+1,first=a->focus>2?a->focus-2:0;if(first>count-4)first=count>4?count-4:0;
                for(int i=0;i<4&&first+i<count;i++){int j=first+i;bool rejected=j && (a->question.rejected&(1u<<(j-1)));snprintf(buf,sizeof(buf),"%s%s",rejected?"x  ":"",j?et_option_name(a->config.mode,a->config.variant,a->question.ids[j-1]):"重听题目");row(i,155+i*33,buf,a->focus==j,rejected);}
            }lv_label_set_text(footer,a->question.done?"确定继续  长按结束":"确定提交  长按结束");
        }
    }
    lv_obj_set_hidden(volume_panel,!(a->volume_until>now));
    if(a->volume_until>now){
        snprintf(buf,sizeof(buf),a->config.volume?"音量  %d%%":"音量  0%%  静音",a->config.volume);
        lv_label_set_text(volume_label,buf);
        lv_obj_set_width(volume_fill,180*a->config.volume/100);
    }

}
