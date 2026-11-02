#include <assert.h>
#include <stdio.h>
#include "../main/hm_audio.c"
static uint64_t clock_ms;static bool locked,auth=true,block_wait;static unsigned accepts,flows,analyzes,steps,notifies;
int64_t esp_timer_get_time(void){return (int64_t)clock_ms*1000;}
bool hm_ble_authenticated(void){return auth;}
int xSemaphoreTake(SemaphoreHandle_t h,TickType_t wait){(void)h;if(locked&&!wait)return 0;if(locked&&block_wait)return 0;locked=true;return pdTRUE;}
void xSemaphoreGive(SemaphoreHandle_t h){(void)h;locked=false;}
void xTaskNotifyGive(TaskHandle_t t){(void)t;notifies++;}
bool hm_core_on_flow(hm_core_t *c,const uint8_t *p,size_t n,uint64_t now){(void)p;(void)n;flows++;if(now-c->last_flow_ms>=3000)return false;c->last_flow_ms=now;return true;}
bool hm_core_audio_accept(hm_core_t *c,const uint8_t *p,size_t n,uint64_t now){(void)p;(void)n;if(now-c->last_pcm_ms>=300)return false;accepts++;c->stats.rx_packets++;c->last_pcm_ms=now;return true;}
void hm_spectrum_reset(hm_spectrum_t *s){memset(s,0,sizeof *s);}
void hm_spectrum_analyze(hm_spectrum_t *s,const int16_t p[256],uint8_t l[16]){(void)s;(void)p;(void)l;analyzes++;}
void hm_spectrum_step(hm_spectrum_t *s,const uint8_t t[16],uint8_t l[16],uint8_t p[16]){(void)s;(void)t;(void)l;(void)p;steps++;}
int main(void){
    hm_core_t core={0};_Atomic uint32_t gen=7;_Atomic bool ptt=false;
    state=&core;guard=(void*)1;link_generation=&gen;physical_ptt=&ptt;available=true;ingress_task=(void*)2;
    core.s.sid=23;core.s.stream_id=17;core.stats_stream=17;core.s.direction=HM_DIRECTION_SPEAKER;
    uint8_t a[100]={1,1,2,160};a[4]=23;a[8]=17;clock_ms=100;
    locked=true;hm_audio_receive(7,a,100);assert(notifies==1&&ingress.count==1&&accepts==0);
    block_wait=true;assert(ingress_step());assert(ingress.count==1&&ingress.deferred==1&&ingress.audio_full==0);
    block_wait=false;clock_ms=120;assert(ingress_step());assert(accepts==1&&ingress.count==0&&core.last_pcm_ms==100&&ingress.deferred==2);assert(rx_timing.queue_max_ms==20);
    clock_ms=200;hm_audio_receive(7,a,100);clock_ms=321;ingress_step();assert(ingress.stale==1&&accepts==1);
    clock_ms=350;hm_audio_receive(7,a,100);clock_ms=400;ingress_step();assert(accepts==1); /* processing-time timeout prevents revival */
    clock_ms=450;hm_audio_receive(7,a,100);gen=8;ingress_step();assert(accepts==1&&ingress.rejected==2);gen=7;
    hm_audio_receive(7,a,100);auth=false;ingress_step();assert(accepts==1);auth=true;
    hm_audio_receive(7,a,100);core.s.stream_id=18;ingress_step();assert(accepts==1);core.s.stream_id=17;
    uint8_t l[16],p[16];int16_t pcm[160]={0};hm_audio_set_spectrum_enabled(false);
    feed_spectrum(pcm,160,clock_ms);hm_audio_spectrum(l,p);assert(!analysis_count&&!analyzes&&!steps);
    hm_audio_set_spectrum_enabled(true);feed_spectrum(pcm,160,clock_ms);feed_spectrum(pcm,160,clock_ms);hm_audio_spectrum(l,p);assert(analyzes==1&&steps==1);
    hm_audio_set_spectrum_enabled(false);feed_spectrum(pcm,160,clock_ms);hm_audio_spectrum(l,p);assert(!analysis_count&&analyzes==1&&steps==1);
    hm_audio_set_spectrum_enabled(true);hm_audio_spectrum(l,p);assert(analyzes==1); /* no stale pre-toggle window */
    clock_ms=1000;core.last_pcm_ms=1000;hm_audio_receive(7,a,100);
    uint8_t credit[24]={1};credit[4]=23;hm_audio_flow(7,credit,24);hm_audio_flow(7,credit,24);
    assert(ingress.flow_replaced==1);unsigned before=accepts;
    ingress_step();assert(flows==1&&accepts==before&&ingress.count==1&&core.last_flow_ms==1000);
    clock_ms=1020;ingress_step();assert(accepts==before+1&&core.last_pcm_ms==1000);
    char out[400];assert(hm_audio_stats_json("rx",out));assert(strlen(out)<400);assert(hm_audio_stats_json("playback",out));assert(strlen(out)<400);
    assert(strstr(out,"playback"));
    ingress.audio_full=ingress.flow_replaced=ingress.stale=ingress.rejected=ingress.deferred=UINT32_MAX;
    core.s.sid=core.stats_stream=UINT32_MAX;timing_sync();assert(rx_timing.queue_max_ms==0);
    rx_timing.count=rx_timing.gap_max_ms=rx_timing.queue_max_ms=UINT32_MAX;
    assert(hm_audio_stats_json("rx",out));assert(strlen(out)<399&&out[strlen(out)-1]=='}');
    assert(strstr(out,"\"queue_max_ms\":4294967295"));
    puts("actual audio callbacks/worker: deferred lock, stale/timeout/identity, spectrum disabled PASS");
}
