#include "ear_model.h"
#include "ear_synth.h"
#include "ear_controller.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    et_config_t c = et_defaults(); assert(c.volume==90); et_question_t q; uint32_t seed=1234;
    for(int mode=0;mode<ET_MODE_COUNT;mode++) for(int level=0;level<3;level++) for(int variant=0;variant<3;variant++) {
        c.mode=mode;c.level=level;c.variant=variant;
        for(int i=0;i<100;i++) {
            et_question(&q,&c,&seed);
            assert(q.count>0 && q.count<=ET_OPTIONS && q.answer<q.count);
            assert(q.score.count>0 && q.score.count<=ET_EVENTS);
            int id=q.ids[q.answer];
            if(mode==ET_INTERVAL){
                if(variant==2)assert(q.score.events[0].notes[1]-q.score.events[0].notes[0]==id+1);
                else assert(abs(q.score.events[0].notes[0]-q.score.events[1].notes[0])==id+1);
            }
            if(mode==ET_NOTE)assert(q.score.events[0].notes[0]==60+id);
            if(mode==ET_SCALE){
                static const int offsets[4][8]={{0,2,4,5,7,9,11,12},{0,2,3,5,7,8,10,12},{0,2,3,5,7,8,11,12},{0,2,3,5,7,9,11,12}};
                for(int k=0;k<8;k++)assert(q.score.events[k].notes[0]==q.root+offsets[id][k]);
            }
            if(mode==ET_CHORD){
                static const int offsets[10][4]={{0,4,7},{0,3,7},{0,3,6},{0,4,8},{0,4,7,10},{0,4,7,11},{0,3,7,10},{0,2,7},{0,5,7},{0,5,7,10}};
                static const int degree[]={0,2,4,5,7,9}, quality[]={0,1,1,0,0,1};
                for(int j=0;j<q.steps;j++){int item=q.ids[q.answers[j]],kind=variant==1?quality[item]:item;for(int k=0;k<q.score.events[j].voices;k++)assert(q.score.events[j].notes[k]==q.root+(variant==1?degree[item]:0)+offsets[kind][k]);}
            }
            for(int j=0;j<q.score.count;j++) {
                assert(q.score.events[j].voices>0 && q.score.events[j].voices<=4);
                for(int k=0;k<q.score.events[j].voices;k++) assert(q.score.events[j].notes[k]>=36 && q.score.events[j].notes[k]<=108);
            }
        }
    }
    c=et_defaults(); assert(c.volume==90); et_question(&q,&c,&seed);
    et_result_t r={0}; assert(et_answer(&r,&q,(q.answer+1)%q.count)==ET_RETRY);
    assert(et_answer(&r,&q,q.answer)==ET_CORRECT); assert(r.first==0 && r.correct==1 && r.total==1);
    assert(et_answer(&r,&q,q.answer)==ET_IGNORED); assert(r.total==1);
    et_question(&q,&c,&seed); assert(et_answer(&r,&q,q.answer)==ET_CORRECT); assert(r.first==1 && r.total==2);
    et_question(&q,&c,&seed); int wrong=(q.answer+1)%q.count;
    assert(et_answer(&r,&q,wrong)==ET_RETRY); assert(et_answer(&r,&q,wrong)==ET_IGNORED);
    assert(et_answer(&r,&q,(wrong+1)%q.count)==(q.answer==(wrong+1)%q.count?ET_CORRECT:ET_WRONG));
    assert(et_tap_error_ms(1100000,1000000,0)==100);
    assert(et_tap_error_ms(1100000,1000000,25)==75);
    assert(et_rhythm_score(0)==100 && et_rhythm_score(300)==0 && et_rhythm_score(-300)==0);
    /* Narrow custom pools cannot generate a question with one answer. */
    c.custom=1; et_question(&q,&c,&seed); assert(q.count>=2);
    /* Exact one-second A4 waveform: verify frequency, nonzero signal, headroom. */
    et_score_t score={.count=1};score.events[0]=(et_note_t){.notes={69},.voices=1,.duration=ET_RATE,.at=0};score.length=ET_RATE;
    et_synth_t synth;et_synth_init(&synth,&score);int16_t pcm[ET_RATE];et_synth_render(&synth,pcm,ET_RATE);
    int crossings=0,peak=0;for(int i=1;i<ET_RATE;i++){if(pcm[i-1]<0&&pcm[i]>=0)crossings++;if(abs(pcm[i])>peak)peak=abs(pcm[i]);}
    assert(abs(crossings-440)<3 && peak>10000 && peak<15000);
    score.events[0].voices=4;for(int i=0;i<4;i++)score.events[0].notes[i]=69+i*4;
    et_synth_init(&synth,&score);et_synth_render(&synth,pcm,ET_RATE);
    for(int i=0;i<ET_RATE;i++)assert(abs(pcm[i])<32000);
    /* Worst-case identical phases: both four- and eight-voice mixes keep headroom. */
    for(int voices=4;voices<=8;voices+=4){
        score.count=(uint8_t)(voices/4);
        for(int j=0;j<score.count;j++){score.events[j]=(et_note_t){.notes={69,69,69,69},.voices=4,.duration=ET_RATE};}
        et_synth_init(&synth,&score);et_synth_render(&synth,pcm,ET_RATE);
        for(int i=0;i<ET_RATE;i++)assert(abs(pcm[i])<30000);
    }
    /* Default question must have reference-level RMS, not a single voice divided by four. */
    c=et_defaults();et_question(&q,&c,&seed);et_synth_init(&synth,&q.score);et_synth_render(&synth,pcm,ET_RATE);
    double energy=0;for(int i=0;i<ET_RATE;i++)energy+=(double)pcm[i]*pcm[i];
    assert(sqrt(energy/ET_RATE)>5000);
    assert(et_codec_level(0)==0 && et_codec_level(25)==70 && et_codec_level(55)==80 && et_codec_level(85)==93 && et_codec_level(100)==100);
    et_synth_init(&synth,&q.score);et_synth_output(&synth,pcm,160,true);
    for(int i=0;i<160;i++)assert(pcm[i]==0);
    assert(synth.cursor==160);et_synth_output(&synth,pcm,160,false);int nonzero=0;
    for(int i=0;i<160;i++)nonzero+=pcm[i]!=0;
    assert(nonzero>0 && synth.cursor==320);
    et_app_t a;et_app_init(&a);a.audio_ok=true;
    et_app_key(&a,2,true,1000);et_app_key(&a,2,false,10000);assert(a.page==ET_SETUP);
    et_app_key(&a,2,true,20000);et_app_key(&a,2,false,30000);assert(a.page==ET_PRACTICE && a.play);
    et_app_key(&a,2,true,40000);et_app_tick(&a,650000);assert(a.page==ET_SUMMARY);
    et_app_key(&a,2,false,660000);assert(a.page==ET_SUMMARY); /* release suppressed */
    a.focus=1;et_app_key(&a,2,true,700000);et_app_key(&a,2,false,710000);assert(a.page==ET_HOME);
    a.focus=ET_NOTE;et_app_key(&a,2,true,720000);et_app_key(&a,2,false,730000);
    a.config.variant=1;a.config.questions=5;et_app_key(&a,2,true,740000);et_app_key(&a,2,false,750000);assert(a.question.count==2);
    for(int i=0;i<5;i++){a.question.done=true;et_app_next(&a);}assert(a.question.count==7);
    a.question.done=true;et_app_next(&a);assert(a.page==ET_SUMMARY && a.session_saved);
    a.page=ET_HOME;a.focus=ET_RHYTHM;et_app_key(&a,2,true,800000);et_app_key(&a,2,false,810000);
    et_app_key(&a,2,true,820000);et_app_key(&a,2,false,830000);
    et_app_audio_start(&a,a.generation,1000000);int64_t origin=a.rhythm_at;
    et_app_key(&a,2,true,origin-300001);et_app_key(&a,2,false,origin-300000);assert(a.taps==0);
    for(int i=0;i<8;i++){int64_t t=origin+(int64_t)a.question.taps[i]*1000000/ET_RATE;et_app_key(&a,2,true,t);et_app_key(&a,2,false,t+1000);}
    assert(a.rhythm_done&&a.rhythm_percent==100&&a.session.correct==8);
    et_app_key(&a,0,true,origin+5000000);et_app_key(&a,0,false,origin+5010000);assert(!a.rhythm_done);et_app_audio_start(&a,a.generation,10000000);
    et_app_tick(&a,30000000);assert(a.rhythm_done&&a.rhythm_percent==0);
    et_app_key(&a,0,true,31000000);et_app_key(&a,0,false,31010000);et_app_audio_start(&a,a.generation,32000000);origin=a.rhythm_at;
    for(int i=0;i<8;i++){int64_t t=origin+(int64_t)a.question.taps[i]*1000000/ET_RATE-50000;et_app_key(&a,2,true,t);et_app_key(&a,2,false,t+1000);}
    assert(a.taps==8&&a.rhythm_percent==80&&a.session.correct==16);
    /* Delayed UI tick: long release must never start a new session. */
    a.page=ET_SETUP;a.focus=0;et_app_key(&a,2,true,50000000);et_app_key(&a,2,false,50700000);assert(a.page==ET_HOME);
    et_app_init(&a);a.audio_ok=true;a.page=ET_PRACTICE;a.session=(et_result_t){.total=2,.first=1,.correct=2};
    et_app_key(&a,2,true,60000000);et_app_key(&a,2,false,60700000);
    assert(a.page==ET_SUMMARY&&a.save&&a.session_saved&&a.history[0].total==2&&a.session.total==2);
    a.focus=1;et_app_key(&a,2,true,61000000);et_app_key(&a,2,false,61001000);assert(a.history[0].total==2);
    et_app_init(&a);a.config.volume=55;int before=a.focus;
    et_app_key(&a,0,true,1000000);assert(a.focus==before);
    et_app_tick(&a,1400000);assert(a.config.volume==60 && a.focus==before && a.volume_changed && !a.save);
    et_app_tick(&a,1850000);assert(a.config.volume==75 && a.focus==before);
    et_app_key(&a,0,false,1900000);assert(a.focus==before && a.save);
    a.save=false;et_app_key(&a,1,true,2000000);et_app_key(&a,1,false,2010000);assert(a.focus==1);
    et_app_key(&a,0,true,3000000);et_app_key(&a,0,false,5000000);assert(a.config.volume==100 && a.focus==1);
    et_app_key(&a,1,true,6000000);et_app_key(&a,1,false,10000000);assert(a.config.volume==0 && a.focus==1);
    et_app_init(&a);a.page=ET_PRACTICE;a.config.mode=ET_RHYTHM;a.generation=17;a.round=2;a.config.volume=55;a.config.volume=55;a.rhythm_at=12000000;
    et_app_key(&a,1,true,11000000);et_app_tick(&a,11550000);et_app_key(&a,1,false,11560000);
    assert(a.page==ET_PRACTICE&&a.generation==17&&a.round==2&&a.config.volume==45&&a.rhythm_at==12000000);
    et_score_t broken={.count=1,.events={{.at=0,.duration=16000,.notes={60,64,67,70},.voices=4}}},expanded;
    assert(et_arpeggiate(&broken,&expanded));assert(expanded.count==5);
    for(int i=0;i<4;i++){assert(expanded.events[i].voices==1 && expanded.events[i].notes[0]==broken.events[0].notes[i]);}
    assert(expanded.events[4].voices==4 && expanded.events[4].at>=expanded.events[3].at+expanded.events[3].duration);
    assert(expanded.length==expanded.events[4].at+expanded.events[4].duration);
    broken.count=4;for(int i=1;i<4;i++)broken.events[i]=broken.events[0];
    assert(et_arpeggiate(&broken,&expanded)&&expanded.count==20);
    for(int i=1;i<expanded.count;i++)assert(expanded.events[i].at>=expanded.events[i-1].at);
    for(int i=0;i<4;i++)assert(expanded.events[i*5+4].voices==4);
    broken.count=6;for(int i=4;i<6;i++)broken.events[i]=broken.events[0];
    assert(!et_arpeggiate(&broken,&expanded)&&expanded.count==0);
    c=et_defaults();c.volume=30;c.questions=20;c.custom=3;
    assert(et_upgrade_config(&c,2)&&c.volume==90&&c.questions==20&&c.custom==3);
    c.volume=25;assert(!et_upgrade_config(&c,ET_SETTINGS_VERSION)&&c.volume==25);
    for(int chord_count=1;chord_count<=4;chord_count+=3){
        et_score_t original={.count=(uint8_t)chord_count},broken;
        for(int j=0;j<chord_count;j++)original.events[j]=(et_note_t){.notes={60,64,67,71},.voices=(uint8_t)(j%2?4:3),.duration=12000,.at=(uint32_t)j*16000};
        assert(et_arpeggiate(&original,&broken));int at=0;
        for(int j=0;j<chord_count;j++){
            const et_note_t *chord=&original.events[j];
            for(int k=0;k<chord->voices;k++,at++){
                assert(broken.events[at].voices==1&&broken.events[at].notes[0]==chord->notes[k]);
                if(k)assert(broken.events[at].at>broken.events[at-1].at);
            }
            const et_note_t *tail=&broken.events[at++];assert(tail->voices==chord->voices);
            for(int k=0;k<chord->voices;k++)assert(tail->notes[k]==chord->notes[k]);
            assert(tail->at>=broken.events[at-2].at+broken.events[at-2].duration);
            if(j+1<chord_count)assert(broken.events[at].at>=tail->at+tail->duration);
            else assert(broken.length==tail->at+tail->duration);
        }
        assert(broken.count==at);
    }
    et_score_t excessive={.count=ET_EVENTS},empty;
    for(int i=0;i<ET_EVENTS;i++)excessive.events[i].voices=4;
    assert(!et_arpeggiate(&excessive,&empty)&&empty.count==0);
    puts("Ear trainer: question pools, attempts, scoring, waveform pitch and mix headroom PASS");
}
