#include "hm_audio.h"
#include "hm_adpcm.h"
#include "hm_ble.h"
#include "hm_spectrum.h"
#include "hm_ingress.h"
#include <stdio.h>
#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <string.h>

/* The core mutex owns BOTH state and these bounded slots. No codec call holds it. */
typedef struct { bool used; uint32_t seq; uint8_t wire[100]; } slot_t;
static slot_t slots[32];
static hm_core_t *state;
static SemaphoreHandle_t guard;
static _Atomic uint32_t *link_generation;
static _Atomic bool available;
static _Atomic bool *physical_ptt;
static uint32_t ring_sid,ring_stream;
static void (*history_received)(const hm_snapshot_t *,uint64_t,void *);
static void *history_ctx;
static int16_t analysis_pcm[256];
static unsigned analysis_count;
static uint32_t analysis_sid,analysis_stream,analysis_revision;
static uint64_t analysis_at;
static hm_spectrum_t spectrum;
static _Atomic bool spectrum_enabled;
static uint32_t spectrum_epoch;
static portMUX_TYPE ingress_mux=portMUX_INITIALIZER_UNLOCKED;
static hm_ingress_t ingress;
static TaskHandle_t ingress_task;
typedef struct {uint32_t count,gap_max_ms,work_max_us,queue_max_ms;uint64_t last_ms;} cadence_t;
static cadence_t rx_timing,play_timing;
static uint32_t timing_sid,timing_stream;
static void timing_sync(void){
    if(state->stats_stream!=timing_stream||state->s.sid!=timing_sid){
        memset(&rx_timing,0,sizeof rx_timing);memset(&play_timing,0,sizeof play_timing);
        timing_sid=state->s.sid;timing_stream=state->stats_stream;
    }
}
static void cadence(cadence_t *t,uint64_t at,uint32_t work){
    if(t->count&&at>=t->last_ms&&at-t->last_ms>t->gap_max_ms)t->gap_max_ms=(uint32_t)(at-t->last_ms);
    t->last_ms=at;t->count++;if(work>t->work_max_us)t->work_max_us=work;
}
void hm_audio_set_history(void (*received)(const hm_snapshot_t *,uint64_t,void *),void *ctx){history_received=received;history_ctx=ctx;}
static void feed_spectrum(const int16_t *pcm,unsigned samples,uint64_t now){
    if(!spectrum_enabled)return;
    if(analysis_sid!=state->s.sid||analysis_stream!=state->s.stream_id){analysis_count=0;memset(analysis_pcm,0,sizeof analysis_pcm);}
    analysis_sid=state->s.sid;analysis_stream=state->s.stream_id;
    memmove(analysis_pcm,analysis_pcm+samples,(256-samples)*sizeof(int16_t));memcpy(analysis_pcm+256-samples,pcm,samples*sizeof(int16_t));
    analysis_count=analysis_count+samples>256?256:analysis_count+samples;analysis_revision++;analysis_at=now;
}
static uint64_t now_ms(void){return esp_timer_get_time()/1000;}
static uint32_t read32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
bool hm_audio_ok(void){return available;}
static void sync_slots(void){
    if(ring_sid!=state->s.sid||ring_stream!=state->s.stream_id||state->s.direction!=HM_DIRECTION_SPEAKER){
        memset(slots,0,sizeof(slots));ring_sid=state->s.sid;ring_stream=state->s.stream_id;
    }
    for(unsigned i=0;i<32;i++)if(slots[i].used&&slots[i].seq<state->s.next_seq)slots[i].used=false;
}
/* NimBLE callbacks only validate transport identity and copy bounded values.
 * They never touch/wait for the core mutex. */
static void enqueue(bool flow,uint32_t gen,const uint8_t *data,size_t len){
    if(!available||!data||len!=(flow?HM_FLOW_BYTES:HM_AUDIO_BYTES)||gen!=*link_generation||!hm_ble_authenticated())return;
    uint64_t at=now_ms();
    portENTER_CRITICAL(&ingress_mux);
    bool added=hm_ingress_push(&ingress,flow,gen,data,at);
    portEXIT_CRITICAL(&ingress_mux);
    if(added)xTaskNotifyGive(ingress_task);
}
void hm_audio_receive(uint32_t gen,const uint8_t *data,size_t len){enqueue(false,gen,data,len);}
void hm_audio_flow(uint32_t gen,const uint8_t *data,size_t len){enqueue(true,gen,data,len);}
static bool ingress_step(void){
    portENTER_CRITICAL(&ingress_mux);bool pending=hm_ingress_pending(&ingress);portEXIT_CRITICAL(&ingress_mux);
    if(!pending)return false;
    if(xSemaphoreTake(guard,0)!=pdTRUE){
        portENTER_CRITICAL(&ingress_mux);ingress.deferred++;portEXIT_CRITICAL(&ingress_mux);
        if(xSemaphoreTake(guard,pdMS_TO_TICKS(10))!=pdTRUE)return true;
    }
    /* Select AFTER acquiring the guard: newly arrived Flow has priority. */
    hm_ingress_item_t item;bool flow;
    portENTER_CRITICAL(&ingress_mux);bool popped=hm_ingress_pop(&ingress,&item,&flow);portEXIT_CRITICAL(&ingress_mux);
    if(!popped){xSemaphoreGive(guard);return true;}
    uint64_t now=now_ms();
    bool current=available&&hm_ingress_current(&item,now,*link_generation,hm_ble_authenticated(),state->s.sid,state->s.stream_id,flow);
    if(!current){
        portENTER_CRITICAL(&ingress_mux);
        if(now>=item.at&&now-item.at>HM_INGRESS_MAX_AGE_MS)ingress.stale++;else ingress.rejected++;
        portEXIT_CRITICAL(&ingress_mux);
    }else if(flow){
        /* Core maintain runs at processing time, before refreshing leases. */
        if(hm_core_on_flow(state,item.data,HM_FLOW_BYTES,now)){state->last_flow_ms=item.at;if(state->s.direction==HM_DIRECTION_MIC)state->mic_flow_ms=item.at;}
        else {portENTER_CRITICAL(&ingress_mux);ingress.rejected++;portEXIT_CRITICAL(&ingress_mux);}
    }else if(hm_core_audio_accept(state,item.data,HM_AUDIO_BYTES,now)){
        state->last_pcm_ms=item.at; /* Queue residence must not extend the receive lease. */
        timing_sync();cadence(&rx_timing,item.at,0);
        if(now-item.at>rx_timing.queue_max_ms)rx_timing.queue_max_ms=(uint32_t)(now-item.at);
        if(history_received)history_received(&state->s,now,history_ctx);
        sync_slots();uint32_t seq=read32(item.data+12);slot_t *s=&slots[seq%32];
        if(s->used&&s->seq!=seq)state->stats.overflow++;
        s->used=true;s->seq=seq;memcpy(s->wire,item.data,HM_AUDIO_BYTES);
    }else {portENTER_CRITICAL(&ingress_mux);ingress.rejected++;portEXIT_CRITICAL(&ingress_mux);}
    xSemaphoreGive(guard);
    return true;
}
static void ingress_worker(void *arg){
    (void)arg;
    for(;;){ulTaskNotifyTake(pdTRUE,portMAX_DELAY);while(ingress_step()){} }
}
static void fault(void){
    available=false;xSemaphoreTake(guard,portMAX_DELAY);hm_core_set_audio_available(state,false);xSemaphoreGive(guard);
    ESP_LOGE("handmic-audio","Codec I/O failed; streams disabled");
}
static void microphone(void *arg){
    (void)arg;int16_t pcm[160];uint8_t wire[HM_AUDIO_BYTES];
    for(;;){
        if(!available){vTaskDelay(pdMS_TO_TICKS(50));continue;}
        xSemaphoreTake(guard,portMAX_DELAY);
        uint32_t sid=state->s.sid,stream=state->s.stream_id,read_gen=*link_generation;
        xSemaphoreGive(guard);
        if(bsp_audio_read(pcm,sizeof pcm)!=ESP_OK){fault();continue;}
        xSemaphoreTake(guard,portMAX_DELAY);uint32_t gen=*link_generation;
        bool current=sid==state->s.sid&&stream==state->s.stream_id&&read_gen==gen;
        bool send=current&&hm_core_mic_frame(state,(const uint8_t *)pcm,now_ms(),wire);
        if(send&&*physical_ptt){
            feed_spectrum(pcm,160,now_ms());
            if(!hm_ble_notify_audio(gen,wire,sizeof wire)){state->stats.overflow++;state->tx_discontinuity=true;}
        }
        xSemaphoreGive(guard);
    }
}
static void speaker(void *arg){
    (void)arg;TickType_t wake=xTaskGetTickCount();uint8_t applied=255;
    for(;;){
        int16_t pcm[160]={0};uint8_t wire[HM_AUDIO_BYTES]={0},volume=0;
        xSemaphoreTake(guard,portMAX_DELAY);sync_slots();uint32_t seq=0;
        uint32_t sid=state->s.sid,stream=state->s.stream_id,gen=*link_generation;
        bool consume=available&&hm_core_speaker_slot(state,now_ms(),&seq),present=false;
        if(consume){
            slot_t *s=&slots[seq%32];
            if(s->used&&s->seq==seq){memcpy(wire,s->wire,sizeof wire);s->used=false;present=true;}
        }
        xSemaphoreGive(guard);
        if(present)(void)hm_adpcm_decode(wire+16,84,pcm);
        xSemaphoreTake(guard,portMAX_DELAY);
        bool current=sid==state->s.sid&&stream==state->s.stream_id&&gen==*link_generation;
        if(current&&state->s.direction==HM_DIRECTION_SPEAKER&&!state->s.muted&&state->s.volume)volume=state->s.volume;
        if(!volume||!current)memset(pcm,0,sizeof pcm);
        if(consume&&current){feed_spectrum(pcm,160,now_ms());timing_sync();cadence(&play_timing,now_ms(),0);}
        xSemaphoreGive(guard);
        if(available){
            if(volume!=applied){bsp_audio_set_volume(volume);applied=volume;}
            int64_t started=esp_timer_get_time();
            if(bsp_audio_write(pcm,sizeof pcm)!=ESP_OK)fault();
            uint32_t work=(uint32_t)(esp_timer_get_time()-started);
            if(consume&&current){xSemaphoreTake(guard,portMAX_DELAY);if(sid==state->s.sid&&stream==state->s.stream_id&&work>play_timing.work_max_us)play_timing.work_max_us=work;xSemaphoreGive(guard);}
        }
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(20));
    }
}
esp_err_t hm_audio_init(hm_core_t *core,SemaphoreHandle_t lock,_Atomic uint32_t *generation,_Atomic bool *ptt){
    state=core;guard=lock;link_generation=generation;physical_ptt=ptt;hm_spectrum_init(&spectrum);
    esp_err_t rc=bsp_audio_init();if(rc==ESP_OK)rc=bsp_audio_set_format(8000,16,1);
    available=rc==ESP_OK;hm_core_set_audio_available(state,available);
    if(!available)return rc;
    bsp_audio_set_volume(0);
    if(xTaskCreate(ingress_worker,"hm-ingress",3072,NULL,5,&ingress_task)!=pdPASS){fault();return ESP_ERR_NO_MEM;}
    if(xTaskCreate(microphone,"hm-mic",4096,NULL,4,NULL)!=pdPASS){fault();return ESP_ERR_NO_MEM;}
    if(xTaskCreate(speaker,"hm-speaker",4096,NULL,4,NULL)!=pdPASS){fault();return ESP_ERR_NO_MEM;}
    return ESP_OK;
}

void hm_audio_spectrum(uint8_t levels[16],uint8_t peaks[16]){
    static uint32_t last_revision,last_sid,last_stream,last_epoch;
    static uint8_t target[16];int16_t pcm[256];uint32_t revision,sid,stream;bool active,fresh;
    if(!spectrum_enabled){memset(levels,0,16);memset(peaks,0,16);return;}
    xSemaphoreTake(guard,portMAX_DELAY);uint64_t now=now_ms();
    uint32_t epoch=spectrum_epoch;
    sid=state->s.sid;stream=state->s.stream_id;revision=analysis_revision;
    active=available&&stream&&((state->s.direction==HM_DIRECTION_MIC&&*physical_ptt)||state->s.direction==HM_DIRECTION_SPEAKER);
    fresh=active&&analysis_sid==sid&&analysis_stream==stream&&analysis_count>=256&&now>=analysis_at&&now-analysis_at<150;
    if(fresh&&revision!=last_revision)memcpy(pcm,analysis_pcm,sizeof pcm);
    xSemaphoreGive(guard);
    if(epoch!=last_epoch||sid!=last_sid||stream!=last_stream||!active){hm_spectrum_reset(&spectrum);memset(target,0,sizeof target);last_sid=sid;last_stream=stream;last_epoch=epoch;}
    if(fresh&&revision!=last_revision)hm_spectrum_analyze(&spectrum,pcm,target);
    else if(!fresh)memset(target,0,sizeof target);
    last_revision=revision;hm_spectrum_step(&spectrum,target,levels,peaks);
}

void hm_audio_set_spectrum_enabled(bool enabled){
    if(!guard)return;
    xSemaphoreTake(guard,portMAX_DELAY);
    if(spectrum_enabled!=enabled){spectrum_enabled=enabled;spectrum_epoch++;analysis_count=0;analysis_sid=analysis_stream=0;analysis_revision++;memset(analysis_pcm,0,sizeof analysis_pcm);}
    xSemaphoreGive(guard);
}
/* Called under the core guard by audio.stats. Never reacquire that guard. */
bool hm_audio_stats_json(const char *section,char out[400]){
    if(!section||!out)return false;
    timing_sync();
    if(!strcmp(section,"rx")){
        uint32_t full,replaced,stale,rejected,deferred,depth;
        portENTER_CRITICAL(&ingress_mux);
        full=ingress.audio_full;replaced=ingress.flow_replaced;stale=ingress.stale;rejected=ingress.rejected;deferred=ingress.deferred;depth=ingress.count;
        portEXIT_CRITICAL(&ingress_mux);
        snprintf(out,400,"{\"section\":\"rx\",\"boot\":{\"queue_drop\":%lu,\"flow_coalesced\":%lu,\"stale\":%lu,\"rejected\":%lu,\"lock_waits\":%lu},\"sid\":%lu,\"stream_id\":%lu,\"scope\":\"latest_stream\",\"active\":%s,\"queue_depth\":%lu,\"accepted\":%lu,\"gap_max_ms\":%lu,\"queue_max_ms\":%lu}",
        (unsigned long)full,(unsigned long)replaced,(unsigned long)stale,(unsigned long)rejected,(unsigned long)deferred,(unsigned long)timing_sid,(unsigned long)timing_stream,state->s.stream_id==timing_stream&&timing_stream?"true":"false",(unsigned long)depth,(unsigned long)rx_timing.count,(unsigned long)rx_timing.gap_max_ms,(unsigned long)rx_timing.queue_max_ms);
    }else if(!strcmp(section,"playback")){
        snprintf(out,400,"{\"section\":\"playback\",\"scope\":\"latest_stream\",\"sid\":%lu,\"stream_id\":%lu,\"active\":%s,\"slots\":%lu,\"gap_max_ms\":%lu,\"write_max_us\":%lu,\"underrun\":%lu,\"seq_missing\":%lu,\"spectrum_enabled\":%s}",(unsigned long)timing_sid,(unsigned long)timing_stream,state->s.stream_id==timing_stream&&timing_stream?"true":"false",(unsigned long)play_timing.count,(unsigned long)play_timing.gap_max_ms,(unsigned long)play_timing.work_max_us,(unsigned long)state->stats.underrun,(unsigned long)state->stats.seq_missing,spectrum_enabled?"true":"false");
    }else return false;
    return true;
}
