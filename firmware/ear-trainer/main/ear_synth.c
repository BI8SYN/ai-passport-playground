#include "ear_synth.h"
#include <stddef.h>
static const int16_t wave[512]={
#include "ear_wave.inc"
};
static const uint32_t increments[109]={
2194674u, 2325176u, 2463438u, 2609922u, 2765116u, 2929538u, 3103738u, 3288296u, 3483828u, 3690987u, 3910465u, 4142993u,
4389348u, 4650352u, 4926877u, 5219844u, 5530232u, 5859077u, 6207476u, 6576592u, 6967656u, 7381975u, 7820930u, 8285986u,
8778697u, 9300705u, 9853754u, 10439689u, 11060465u, 11718154u, 12414952u, 13153184u, 13935313u, 14763950u, 15641860u, 16571973u,
17557394u, 18601411u, 19707508u, 20879378u, 22120930u, 23436309u, 24829905u, 26306368u, 27870626u, 29527900u, 31283720u, 33143947u,
35114788u, 37202822u, 39415017u, 41758756u, 44241861u, 46872619u, 49659810u, 52612736u, 55741252u, 59055800u, 62567440u, 66287894u,
70229577u, 74405645u, 78830035u, 83517513u, 88483723u, 93745239u, 99319621u, 105225473u, 111482505u, 118111600u, 125134881u, 132575789u,
140459155u, 148811291u, 157660071u, 167035027u, 176967447u, 187490479u, 198639243u, 210450947u, 222965011u, 236223201u, 250269763u, 265151578u,
280918311u, 297622583u, 315320143u, 334070054u, 353934894u, 374980958u, 397278486u, 420901894u, 445930023u, 472446402u, 500539527u, 530303156u,
561836623u, 595245167u, 630640287u, 668140109u, 707869788u, 749961916u, 794556972u, 841803788u, 891860046u, 944892805u, 1001079055u, 1060606313u,
1123673246u
};
#include <string.h>
void et_synth_init(et_synth_t *s,const et_score_t *score){
    memset(s,0,sizeof(*s));s->mix_divisor=1;if(score)s->score=*score;
    unsigned maximum=1;
    for(unsigned i=0;i<s->score.count;i++){
        unsigned simultaneous=0;
        for(unsigned j=0;j<s->score.count;j++){
            const et_note_t *n=&s->score.events[j];
            if(n->at<=s->score.events[i].at && n->at+n->duration>s->score.events[i].at)simultaneous+=n->voices;
        }
        if(simultaneous>maximum)maximum=simultaneous;
    }
    /* Fixed for the entire sequence: balanced RMS without gain pumping at note-off.
       At most four voices /2, or eight /4: worst-case in-phase peak stays below full scale. */
    s->mix_divisor=maximum==1?1:maximum<=4?2:4;
}
static void trigger(et_synth_t *s,const et_note_t *n){
    for(int k=0;k<n->voices;k++)for(int i=0;i<8;i++)if(!s->voices[i].active){
        s->voices[i]=(et_voice_t){.active=true,.duration=n->duration,.increment=increments[n->notes[k]<=108?n->notes[k]:69]};break;
    }
}
void et_synth_render(et_synth_t *s,int16_t *out,unsigned count){
    for(unsigned i=0;i<count;i++,s->cursor++){
        while(s->next<s->score.count && s->score.events[s->next].at<=s->cursor)trigger(s,&s->score.events[s->next++]);
        int32_t sum=0;
        for(int j=0;j<8;j++){
            et_voice_t *v=&s->voices[j];if(!v->active)continue;
            if(v->age>=v->duration){v->active=false;continue;}
            uint32_t remaining=v->duration-v->age;
            int32_t env=v->age<80?(int32_t)v->age*32767/80:32767;
            if(remaining<320)env=env*(int32_t)remaining/320;
            /* Gentle piano-like decay, integer envelope per sample. */
            env=env*8192/(8192+(int32_t)v->age);
            sum+=(int32_t)wave[v->phase>>23]*env/32768/s->mix_divisor;
            v->phase+=v->increment;v->age++;
        }
        if(sum>32767)sum=32767;
        if(sum< -32767)sum=-32767;
        out[i]=(int16_t)sum;
    }
}

/* Digital mute retains the sample clock; codec volume zero alone is only -50dB. */
void et_synth_output(et_synth_t *s,int16_t *out,unsigned count,bool muted){
    et_synth_render(s,out,count);
    if(muted)memset(out,0,count*sizeof(*out));
}
