#include "ear_controller.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static void change_page(et_app_t *a,et_page_t page){a->page=page;a->focus=0;a->dirty=true;a->generation++;a->rhythm_at=0;}
void et_app_init(et_app_t *a){memset(a,0,sizeof(*a));a->config=et_defaults();a->battery=-1;a->axis_key=-1;a->seed=654321;a->dirty=true;}
static void new_question(et_app_t *a){et_config_t c=a->config;
    if(c.mode==ET_NOTE&&c.variant==1){static const int keys[]={0,4,7,2,9,5,11};c.custom=0;int n=a->round+1;if(n>7)n=7;for(int i=0;i<n;i++)c.custom|=1u<<keys[i];}
    et_question(&a->question,&c,&a->seed);a->focus=0;a->verdict=ET_IGNORED;a->arpeggio=false;a->generation++;a->play=true;a->dirty=true;a->taps=0;a->rhythm_done=false;a->rhythm_at=0;memset(a->errors,0,sizeof(a->errors));}
static void persist(et_app_t *a){if(a->session_saved)return;a->session_saved=true;et_result_t *h=&a->history[a->config.mode*3+a->config.variant];h->total+=a->session.total;h->first+=a->session.first;h->correct+=a->session.correct;for(int i=0;i<ET_OPTIONS;i++)h->errors[i]+=a->session.errors[i];a->save=true;}
static void end_session(et_app_t *a){persist(a);change_page(a,ET_SUMMARY);}
static void start(et_app_t *a){if(!a->audio_ok)return;memset(&a->session,0,sizeof(a->session));a->round=1;a->session_saved=false;change_page(a,ET_PRACTICE);new_question(a);}
void et_app_next(et_app_t *a){
    if(et_next_step(&a->question)){a->focus=0;a->verdict=ET_IGNORED;a->dirty=true;return;}
    int goal=a->config.mode==ET_NOTE&&a->config.variant==1?6:a->config.questions;
    if(goal && a->round>=goal){end_session(a);return;}
    a->round++;new_question(a);
}
static void replay(et_app_t *a){a->generation++;a->play=true;a->dirty=true;}
int et_setting_count(void){return 10;}
void et_setting_text(const et_app_t *a,int row,char *out,unsigned size){
    const et_config_t *c=&a->config;const char *levels[]={"入门","标准","进阶"};const char *speeds[]={"慢","中","快"};
    const char *variants=c->mode==ET_INTERVAL?(c->variant==0?"上行":c->variant==1?"下行":"同时"):
        c->mode==ET_CHORD?(c->variant==1?"和弦进行":"单和弦"):
        c->mode==ET_RHYTHM?(c->variant==1?"变化乐句":"匀速旋律"):
        c->mode==ET_NOTE?(c->variant==1?"阶梯挑战":"固定音池"):"听辨";
    switch(row){
        case 0:snprintf(out,size,a->audio_ok?"开始训练":"音频初始化失败");break;
        case 1:snprintf(out,size,"难度          %s",levels[c->level]);break;
        case 2:snprintf(out,size,"模式          %s",variants);break;
        case 3:snprintf(out,size,"速度          %s",speeds[c->speed]);break;
        case 4:if(c->questions)snprintf(out,size,"题量          %d",c->questions);else snprintf(out,size,"题量          不限");break;
        case 5:if(c->mode==ET_NOTE)snprintf(out,size,"音域          C4 - B5");else if(c->mode==ET_RHYTHM)snprintf(out,size,"跟拍          八次打点");else snprintf(out,size,"主音          %s",c->fixed?"固定 C":"随机");break;
        case 6:snprintf(out,size,"音量          %d%%",c->volume);break;
        case 7:if(c->mode==ET_NOTE&&c->variant==1)snprintf(out,size,"题池          自动扩展");else if(c->mode==ET_RHYTHM)snprintf(out,size,"时序偏置     %+d ms",c->offset_ms);else snprintf(out,size,"自选题池     %s",c->custom?"已启用":"预设");break;
        case 8:snprintf(out,size,"我的成绩");break;
        default:snprintf(out,size,"返回首页");break;
    }
}
static void back(et_app_t *a){
    if(a->page==ET_HOME)return;
    if(a->page==ET_PRACTICE){end_session(a);return;}
    if(a->page==ET_SUMMARY){persist(a);change_page(a,ET_SETUP);return;}
    if(a->page==ET_POOL||a->page==ET_STATS){change_page(a,ET_SETUP);a->save=true;return;}
    change_page(a,ET_HOME);a->save=true;
}
static void activate(et_app_t *a){
    et_config_t *c=&a->config;int f=a->focus;
    if(a->page==ET_HOME){if(c->mode!=f){c->variant=0;c->custom=0;}c->mode=(uint8_t)f;change_page(a,ET_SETUP);}
    else if(a->page==ET_SETUP){
        if(f==0){start(a);return;}
        if(f==1){c->level=(c->level+1)%3;c->custom=0;}
        if(f==2){c->variant=(c->variant+1)%(c->mode==ET_INTERVAL?3:(c->mode==ET_CHORD||c->mode==ET_NOTE||c->mode==ET_RHYTHM?2:1));c->custom=0;}
        if(f==3)c->speed=(c->speed+1)%3;
        if(f==4)c->questions=c->questions==5?10:c->questions==10?20:c->questions==20?0:5;
        if(f==5&&c->mode<ET_NOTE)c->fixed=!c->fixed;
        if(f==6){c->volume=c->volume>=100?0:c->volume+5;if(c->volume>100)c->volume=100;a->volume_changed=true;}
        if(f==7&&!(c->mode==ET_NOTE&&c->variant==1)){if(c->mode==ET_RHYTHM)c->offset_ms=c->offset_ms>=150?-150:c->offset_ms+10;else{
            if(!c->custom){et_question_t q;et_question(&q,c,&a->seed);for(int i=0;i<q.count;i++)c->custom|=1u<<q.ids[i];}
            change_page(a,ET_POOL);
        }}
        if(f==8)change_page(a,ET_STATS);
        if(f==9)change_page(a,ET_HOME);
        a->save=true;
    }else if(a->page==ET_POOL){
        if(f==0){change_page(a,ET_SETUP);a->save=true;}
        else{uint32_t mask=c->custom^(1u<<(f-1));if(mask&&(mask&(mask-1)))c->custom=mask;a->save=true;}
    }else if(a->page==ET_STATS){change_page(a,ET_SETUP);}
    else if(a->page==ET_SUMMARY){persist(a);if(f==0)start(a);else change_page(a,ET_HOME);}
    else if(a->page==ET_PRACTICE){
        if(c->mode==ET_RHYTHM){if(a->rhythm_done)et_app_next(a);return;}
        if(a->question.done){if(f==0)et_app_next(a);else{a->arpeggio=f==2;replay(a);}}
        else if(f==0){a->arpeggio=false;replay(a);}
        else{a->verdict=et_answer(&a->session,&a->question,f-1);if(a->question.done)a->focus=0;}
    }
    a->dirty=true;
}
static void rhythm_end(et_app_t *a){
    int sum=0;for(int i=0;i<ET_TAPS;i++){int score=et_rhythm_score(a->errors[i]);sum+=score;a->session.total++;if(abs(a->errors[i])<=80){a->session.first++;a->session.correct++;}else a->session.errors[0]++;}
    a->rhythm_percent=sum/ET_TAPS;a->rhythm_done=true;a->dirty=true;
}
void et_app_audio_start(et_app_t *a,uint32_t generation,int64_t at){if(a->generation!=generation||a->page!=ET_PRACTICE)return;a->rhythm_at=at+(int64_t)a->question.score.length*1000000/ET_RATE;a->dirty=true;}
/* Decide UP/DOWN on release: a hold must never navigate or restart a rhythm trial. */
static void volume_hold(et_app_t *a,int64_t now){
    if(a->axis_key<0||now-a->axis_at<400000)return;
    int64_t steps=1+(now-a->axis_at-400000)/150000;
    if(steps<=a->axis_steps)return;
    int64_t delta=steps-a->axis_steps;if(delta>20)delta=20;
    int value=a->config.volume+(a->axis_key==0?1:-1)*(int)delta*5;
    if(value<0)value=0;
    if(value>100)value=100;
    if(value!=a->config.volume){a->config.volume=(uint8_t)value;a->volume_changed=true;}
    a->axis_steps=steps;a->volume_until=now+1200000;a->dirty=true;
}
void et_app_key(et_app_t *a,int key,bool down,int64_t now){
    if(key==2){
        if(down){if(a->ok_down)return;a->ok_down=true;a->held=false;a->pressed_at=now;
            if(a->page==ET_PRACTICE&&a->config.mode==ET_RHYTHM&&!a->rhythm_done){
                a->held=true;
                if(a->rhythm_at && now>=a->rhythm_at-250000+(int64_t)a->config.offset_ms*1000 && a->taps<ET_TAPS){int i=a->taps++;a->errors[i]=et_tap_error_ms(now,a->rhythm_at+(int64_t)a->question.taps[i]*1000000/ET_RATE,a->config.offset_ms);a->dirty=true;if(a->taps==ET_TAPS)rhythm_end(a);}
            }
        }else{if(a->ok_down&&!a->held){if(now-a->pressed_at>=600000){a->held=true;back(a);}else activate(a);}a->ok_down=false;}
        return;
    }
    if(key<0||key>1)return;
    if(down){
        if(a->axis_key!=-1)return;
        a->axis_key=key;a->axis_at=now;a->axis_steps=0;a->axis_volume=a->config.volume;a->axis_generation=a->generation;
        return;
    }
    if(a->axis_key!=key)return;
    volume_hold(a,now);bool is_volume=a->axis_steps>0;a->axis_key=-1;
    if(is_volume){if(a->config.volume!=a->axis_volume)a->save=true;return;}
    if(a->axis_generation!=a->generation)return;
    if(a->page==ET_PRACTICE&&a->config.mode==ET_RHYTHM){if(key==0)new_question(a);else end_session(a);return;}
    int count=a->page==ET_HOME?5:a->page==ET_SETUP?et_setting_count():a->page==ET_POOL?1+et_pool_size(a->config.mode,a->config.variant):a->page==ET_SUMMARY?2:a->page==ET_STATS?1:a->question.done?(a->config.mode==ET_CHORD?3:2):1+a->question.count;
    a->focus=(a->focus+(key==0?-1:1)+count)%count;a->dirty=true;
}
void et_app_tick(et_app_t *a,int64_t now){
    volume_hold(a,now);
    if(a->volume_until && now>=a->volume_until){a->volume_until=0;a->dirty=true;}
    if(a->ok_down&&!a->held&&now-a->pressed_at>=600000){a->held=true;back(a);}
    if(a->page==ET_PRACTICE&&a->config.mode==ET_RHYTHM&&!a->rhythm_done&&a->rhythm_at){
        int64_t last=a->rhythm_at+(int64_t)a->question.taps[ET_TAPS-1]*1000000/ET_RATE+800000;
        if(now>last){while(a->taps<ET_TAPS)a->errors[a->taps++]=10000;rhythm_end(a);}
    }
}
