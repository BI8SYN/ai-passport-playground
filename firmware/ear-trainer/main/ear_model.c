#include "ear_model.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static const char *modes[]={"音程听辨","和弦训练","音阶听辨","单音识别","节奏挑战"};
static const char *intervals[]={"小二度","大二度","小三度","大三度","纯四度","三全音","纯五度","小六度","大六度","小七度","大七度","纯八度","小九度","大九度"};
static const char *chords[]={"大三和弦","小三和弦","减三和弦","增三和弦","属七和弦","大七和弦","小七和弦","挂二和弦","挂四和弦","七挂四和弦"};
static const char *degrees[]={"I","ii","iii","IV","V","vi"};
static const char *scales[]={"大调","自然小调","和声小调","旋律小调"};
static const uint8_t chord_notes[10][4]={{0,4,7},{0,3,7},{0,3,6},{0,4,8},{0,4,7,10},{0,4,7,11},{0,3,7,10},{0,2,7},{0,5,7},{0,5,7,10}};
static const uint8_t scale_notes[4][8]={{0,2,4,5,7,9,11,12},{0,2,3,5,7,8,10,12},{0,2,3,5,7,8,11,12},{0,2,3,5,7,9,11,12}};
static uint32_t random_next(uint32_t *state){uint32_t x=*state?*state:1;x^=x<<13;x^=x>>17;x^=x<<5;return *state=x;}
static int pick(uint32_t *s,int n){return (int)(random_next(s)%(unsigned)n);}
et_config_t et_defaults(void){return (et_config_t){.volume=90,.questions=10,.fixed=true,.speed=1};}
const char *et_mode_name(int mode){return modes[mode>=0&&mode<ET_MODE_COUNT?mode:0];}
void et_note_name(int midi,char *out,unsigned size){static const char *names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};snprintf(out,size,"%s%d",names[(midi%12+12)%12],midi/12-1);}
int et_pool_size(int mode,int variant){return mode==ET_INTERVAL?14:mode==ET_CHORD?(variant==1?6:10):mode==ET_SCALE?4:mode==ET_NOTE?24:1;}
const char *et_option_name(int mode,int variant,int id){
    static const char *notes[]={"C4","C#4","D4","D#4","E4","F4","F#4","G4","G#4","A4","A#4","B4","C5","C#5","D5","D#5","E5","F5","F#5","G5","G#5","A5","A#5","B5"};
    if(id<0||id>=et_pool_size(mode,variant))return "?";
    return mode==ET_INTERVAL?intervals[id]:mode==ET_CHORD?(variant==1?degrees[id]:chords[id]):mode==ET_SCALE?scales[id]:mode==ET_NOTE?notes[id]:"跟拍";
}
static void add(et_score_t *s,uint32_t at,uint32_t duration,int root,const uint8_t *offsets,int count){
    if(s->count>=ET_EVENTS)return;
    et_note_t *n=&s->events[s->count++];*n=(et_note_t){.at=at,.duration=duration,.voices=count};
    for(int i=0;i<count;i++)n->notes[i]=(uint8_t)(root+(offsets?offsets[i]:0));
    if(at+duration>s->length)s->length=at+duration;
}
void et_question(et_question_t *q,const et_config_t *c,uint32_t *seed){
    memset(q,0,sizeof(*q));q->steps=1;q->root=(uint8_t)(60+(c->fixed?0:pick(seed,12)));
    int pool=et_pool_size(c->mode,c->variant);uint32_t mask=0;
    if(c->custom)mask=c->custom&((1u<<pool)-1);
    if(!mask || !(mask&(mask-1))){
        if(c->mode==ET_INTERVAL)mask=c->level==0?0x1f:c->level==1?0xfff:0x3fff;
        else if(c->mode==ET_CHORD)mask=c->variant==1?(c->level==0?0x19:0x3f):(c->level==0?3:c->level==1?0x7f:0x3ff);
        else if(c->mode==ET_SCALE)mask=c->level==0?3:15;
        else if(c->mode==ET_NOTE)mask=c->level==0?0x15:c->level==1?0xab5:0xffffff;
        else mask=1;
    }
    for(int i=0;i<pool;i++)if(mask&(1u<<i))q->ids[q->count++]=(uint8_t)i;
    q->answer=(uint8_t)pick(seed,q->count);q->answers[0]=q->answer;
    uint32_t beat=c->speed==0?11200:c->speed==1?8000:5600;
    int id=q->ids[q->answer];
    if(c->mode==ET_INTERVAL){
        int distance=id+1;
        if(c->variant==2){uint8_t pair[]={0,(uint8_t)distance};add(&q->score,0,beat*2,q->root,pair,2);}
        else{int a=q->root+(c->variant==1?distance:0),b=q->root+(c->variant==1?0:distance);add(&q->score,0,beat*3/4,a,0,1);add(&q->score,beat,beat,b,0,1);}
    }else if(c->mode==ET_CHORD){
        q->steps=c->variant==1?4:1;
        static const int offsets[]={0,2,4,5,7,9},qualities[]={0,1,1,0,0,1};
        for(int i=0;i<q->steps;i++){
            q->answers[i]=(uint8_t)(i?pick(seed,q->count):q->answer);int item=q->ids[q->answers[i]];
            int quality=c->variant==1?qualities[item]:item,root=q->root+(c->variant==1?offsets[item]:0);
            int voices=(quality>=4&&quality<=6)||quality==9?4:3;
            add(&q->score,i*beat*2,beat*3/2,root,chord_notes[quality],voices);
        }
    }else if(c->mode==ET_SCALE){for(int i=0;i<8;i++)add(&q->score,i*beat/2,beat*2/5,q->root+scale_notes[id][i],0,1);q->score.length=beat*4;}
    else if(c->mode==ET_NOTE){add(&q->score,0,beat*2,60+id,0,1);q->root=(uint8_t)(60+id);}
    else{
        static const uint8_t melody[]={0,4,7,4,5,2,7,0};static const uint8_t pattern[]={0,2,4,7,8,11,12,14};
        for(int i=0;i<8;i++){q->taps[i]=c->variant==1?pattern[i]*beat/2:i*beat;add(&q->score,q->taps[i],beat/3,60+melody[i],0,1);}
        for(int i=0;i<4;i++)add(&q->score,(8+i)*beat,beat/5,i==0?84:79,0,1);
        q->score.length=12*beat;
    }
}
et_verdict_t et_answer(et_result_t *r,et_question_t *q,int option){
    if(q->done||option<0||option>=q->count||(q->rejected&(1u<<option)))return ET_IGNORED;
    q->attempts++;if(option==q->answer){r->first+=q->attempts==1;r->correct++;r->total++;q->done=true;return ET_CORRECT;}
    q->rejected|=1u<<option;
    if(q->attempts==1){r->errors[q->ids[q->answer]]++;return ET_RETRY;}
    r->total++;q->done=true;return ET_WRONG;
}
bool et_next_step(et_question_t *q){if(q->step+1>=q->steps)return false;q->step++;q->answer=q->answers[q->step];q->done=false;q->attempts=0;q->rejected=0;return true;}
int et_tap_error_ms(int64_t actual,int64_t expected,int offset_ms){int64_t d=(actual-expected)/1000-offset_ms;if(d>10000)d=10000;if(d< -10000)d=-10000;return (int)d;}
int et_rhythm_score(int error_ms){int a=abs(error_ms);return a>=250?0:100-a*100/250;}

/* Keep the default reference level; expose the DAC full range above it. */
int et_codec_level(unsigned ui_percent){
    if(!ui_percent)return 0;
    if(ui_percent>100)ui_percent=100;
    if(ui_percent<=55)return (int)((185+ui_percent)/3);
    return (int)(80+(ui_percent-55)*20/45);
}

bool et_upgrade_config(et_config_t *c,unsigned stored_version){
    if(stored_version>=1 && stored_version<ET_SETTINGS_VERSION){c->volume=et_defaults().volume;return true;}
    return false;
}
/* Rebuild each chord as individual tones, a short gap, then the complete chord.
   Sequence time grows so a following chord never overlaps the closing chord. */
bool et_arpeggiate(const et_score_t *source,et_score_t *out){
    if(source==out)return false;
    memset(out,0,sizeof(*out));
    if(source->count>ET_EVENTS)return false;
    unsigned required=0;
    for(unsigned j=0;j<source->count;j++){
        unsigned voices=source->events[j].voices;
        if(voices<1||voices>4)return false;
        required+=voices+1;
    }
    if(required>ET_EVENTS)return false;
    uint32_t cursor=0;
    for(unsigned j=0;j<source->count;j++){
        const et_note_t *chord=&source->events[j];
        for(unsigned k=0;k<chord->voices;k++){
            et_note_t n=*chord;n.at=cursor+k*2400;n.duration=4000;n.notes[0]=chord->notes[k];n.voices=1;
            out->events[out->count++]=n;
        }
        et_note_t complete=*chord;
        complete.at=cursor+(chord->voices-1)*2400+4000+1600;
        complete.duration=11200;
        out->events[out->count++]=complete;
        out->length=complete.at+complete.duration;cursor=out->length+3200;
    }
    return true;
}
