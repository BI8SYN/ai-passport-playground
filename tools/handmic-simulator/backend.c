/* Diagnostic host runtime. Product/core/UI sources are linked read-only.
 * stdout is exclusively NDJSON; no PCM is saved to disk by this process. */
#include "hm_core.h"
#include "hm_adpcm.h"
#include "hm_product.h"
#include "hm_ui.h"
#include "hm_fragment.h"
#include "hm_spectrum.h"
#include "cJSON.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define INPUT_MAX 65536
#define DEVICE_ID "be0f4d8c-0000-4000-8000-000000000001"
typedef struct { bool used; uint32_t seq; int16_t pcm[160]; } slot_t;
static hm_core_t core;
static hm_product_t product;
static hm_fragment_t fragments;
static hm_spectrum_t spectrum;
static slot_t slots[32];
static uint32_t ring_sid, ring_stream, output_transfer;
static uint16_t pixels[240*320], previous_pixels[240*320], draw_buffer[240*20];
static bool first_frame=true, running=true, connected, subscribed, settings_fault;
static bool advertised, advertised_known;
static uint16_t max_value=20;
static char central[129], saved_central[129], saved_theme[32]="phosphor_green";
static uint8_t saved_volume=90;
static bool saved_spectrum, spectrum_mode, spectrum_mode_known;
static const char *settings_path;
static uint64_t epoch_ms, pair_deadline, flow_at, ui_at, metrics_at, last_tick;
static uint32_t last_flow_next, input_errors, audio_accepted, audio_rejected, mic_input, mic_dropped;
static uint32_t last_flow_sid,last_flow_stream,last_flow_press;
static bool last_flow_down;
static uint32_t flow_rejected, playback_frames, clock_overruns;
static uint32_t playback_sid,playback_stream;
static hm_direction_t playback_direction;
static bool playback_source_known;
static uint32_t control_rejected;
static uint64_t arrival_last_ns, arrival_count;
static uint32_t arrival_sid,arrival_stream;
static double arrival_mean_ms, arrival_m2, arrival_min_ms, arrival_max_ms;
static int16_t analysis_pcm[256];
static unsigned analysis_count;
static uint64_t analysis_at;
static uint32_t analysis_sid, analysis_stream, analysis_revision, analyzed_revision;
static uint32_t spectrum_sid, spectrum_stream;
static uint8_t spectrum_target[16];
static char previous_info[513], previous_status[513];
static lv_display_t *display;

static uint64_t absolute_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static uint64_t now_ms(void) { return absolute_ms()-epoch_ms; }
static cJSON *obj(const char *type) { cJSON *j=cJSON_CreateObject(); cJSON_AddStringToObject(j,"type",type); return j; }
static void emit(cJSON *j) {
    char *s=cJSON_PrintUnformatted(j); cJSON_Delete(j);
    if(!s) { running=false; return; }
    if(puts(s)==EOF||fflush(stdout)!=0) running=false;
    cJSON_free(s);
}
static void event(const char *message) { cJSON *j=obj("event"); cJSON_AddStringToObject(j,"message",message); emit(j); }
static void emit_bool(const char *type,const char *key,bool v) { cJSON *j=obj(type); cJSON_AddBoolToObject(j,key,v); emit(j); }
static void hex_event(const char *type,const char *channel,const uint8_t *data,size_t length) {
    static const char digits[]="0123456789abcdef";
    char *hex=malloc(length*2+1); if(!hex) { running=false; return; }
    for(size_t i=0;i<length;i++) { hex[i*2]=digits[data[i]>>4]; hex[i*2+1]=digits[data[i]&15]; }
    hex[length*2]=0;
    cJSON *j=obj(type); if(channel)cJSON_AddStringToObject(j,"channel",channel);
    cJSON_AddStringToObject(j,"hex",hex);
    if(!strcmp(type,"frame")){cJSON_AddNumberToObject(j,"width",240);cJSON_AddNumberToObject(j,"height",320);}
    emit(j); free(hex);
}
static int nibble(char c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
static size_t unhex(const cJSON *j,uint8_t *out,size_t capacity) {
    if(!cJSON_IsString(j)||!j->valuestring)return SIZE_MAX;
    size_t n=strlen(j->valuestring); if(n%2||n/2>capacity)return SIZE_MAX;
    for(size_t i=0;i<n/2;i++){int a=nibble(j->valuestring[i*2]),b=nibble(j->valuestring[i*2+1]);if(a<0||b<0)return SIZE_MAX;out[i]=(uint8_t)((a<<4)|b);}
    return n/2;
}
static cJSON *get(const cJSON *j,const char *k){return cJSON_GetObjectItemCaseSensitive(j,k);}
static const char *text_value(const cJSON *j,const char *k){cJSON *v=get(j,k);return cJSON_IsString(v)?v->valuestring:NULL;}
static uint32_t read32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool window_open(uint64_t now){return pair_deadline&&now<pair_deadline;}
static bool bonded(void){return saved_central[0]!=0;}
static bool trusted(void){return connected&&central[0]&&(!strcmp(central,saved_central)||(!bonded()&&window_open(now_ms())));}

static bool save_settings(uint8_t volume,const char *theme,const char *peer,bool spectrum) {
    cJSON *j=cJSON_CreateObject();cJSON_AddNumberToObject(j,"volume",volume);
    cJSON_AddStringToObject(j,"theme",theme);cJSON_AddStringToObject(j,"virtualcentral",peer);cJSON_AddBoolToObject(j,"spectrum",spectrum);
    char *data=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!data)return false;
    char temporary[4096];int len=snprintf(temporary,sizeof temporary,"%s.tmp.%ld",settings_path,(long)getpid());
    bool ok=len>0&&(size_t)len<sizeof temporary;
    int fd=ok?open(temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600):-1;
    if(fd<0)ok=false;
    if(ok){size_t n=strlen(data),off=0;while(off<n){ssize_t w=write(fd,data+off,n-off);if(w<0&&errno==EINTR)continue;if(w<=0){ok=false;break;}off+=(size_t)w;}if(ok&&fsync(fd))ok=false;if(close(fd))ok=false;}
    if(ok&&rename(temporary,settings_path))ok=false;
    if(fd>=0&&!ok)unlink(temporary);
    cJSON_free(data); return ok;
}
static bool local_volume(uint8_t v,void *ctx){(void)ctx;if(!save_settings(v,saved_theme,saved_central,saved_spectrum))return false;saved_volume=v;settings_fault=false;return true;}
static bool store_theme(const char *id,void *ctx){(void)ctx;if(!save_settings(saved_volume,id,saved_central,saved_spectrum))return false;snprintf(saved_theme,sizeof saved_theme,"%s",id);settings_fault=false;return true;}
static bool store_spectrum(bool enabled,void *ctx){(void)ctx;if(!save_settings(saved_volume,saved_theme,saved_central,enabled))return false;saved_spectrum=enabled;settings_fault=false;return true;}
static bool remote_volume(uint8_t v,void *ctx){(void)ctx;if(hm_product_volume_busy(&product)||!local_volume(v,NULL))return false;hm_product_remote_volume(&product,v);return true;}
static void load_settings(void) {
    int fd=open(settings_path,O_RDONLY|O_NOFOLLOW);if(fd<0){settings_fault=errno!=ENOENT;return;}
    char data[4097];ssize_t n=read(fd,data,sizeof data-1);close(fd);
    if(n<=0||n>=4096){settings_fault=true;return;}data[n]=0;
    const char *end=NULL;cJSON *j=cJSON_ParseWithLengthOpts(data,(size_t)n+1,&end,true);
    if(!cJSON_IsObject(j)){settings_fault=true;cJSON_Delete(j);return;}
    cJSON *v=get(j,"volume");const char *theme=text_value(j,"theme"),*peer=text_value(j,"virtualcentral");
    if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>100||floor(v->valuedouble)!=v->valuedouble||!theme||strlen(theme)>=sizeof saved_theme||!peer||strlen(peer)>=sizeof saved_central){settings_fault=true;cJSON_Delete(j);return;}
    saved_volume=(uint8_t)v->valueint;snprintf(saved_theme,sizeof saved_theme,"%s",hm_product_palette(hm_product_theme_from_id(theme))->id);
    saved_spectrum=cJSON_IsTrue(get(j,"spectrum"));snprintf(saved_central,sizeof saved_central,"%s",peer);cJSON_Delete(j);
}
static void clear_arrival_stats(void) {
    arrival_last_ns=arrival_count=0;
    arrival_mean_ms=arrival_m2=arrival_min_ms=arrival_max_ms=0;
}
/* Retain the latest completed speaker stream for diagnostics until a new stream
 * starts; never join intervals across stream stops or session generations. */
static void sync_arrival_scope(void) {
    if(arrival_sid!=core.s.sid){clear_arrival_stats();arrival_sid=core.s.sid;arrival_stream=0;}
    if(core.s.direction==HM_DIRECTION_SPEAKER&&arrival_stream!=core.s.stream_id){clear_arrival_stats();arrival_stream=core.s.stream_id;}
    if(core.s.direction!=HM_DIRECTION_SPEAKER)arrival_last_ns=0;
}
static void observe(uint64_t now) { sync_arrival_scope();hm_snapshot_t s;hm_core_get_snapshot(&core,now,&s);hm_product_observe(&product,&s,bonded(),core.s.connected,window_open(now),now); }
static void advertise(uint64_t now) {
    bool enabled=!connected&&(window_open(now)||bonded());
    if(!advertised_known||enabled!=advertised){advertised=enabled;advertised_known=true;emit_bool("advertise","enabled",enabled);}
}
static void send_control(const char *json,bool hello) {
    if(!json[0]||!core.s.connected)return;
    uint16_t limit=hello||!core.s.sid?20:core.value_max;
    size_t total=strlen(json),off=0;uint8_t wire[244];uint32_t transfer=++output_transfer;
    while(off<total){size_t n=hm_fragment_encode(wire,sizeof wire,transfer,(uint16_t)off,(const uint8_t *)json,total,limit);if(!n){input_errors++;event("control fragment encoding failed");return;}hex_event("ble_notify","control",wire,n);core.transport_boot_submitted++;off+=n-8;}
}
static void close_link(void) {
    hm_core_close(&core);hm_fragment_reset(&fragments);memset(slots,0,sizeof slots);
    connected=false;subscribed=false;central[0]=0;
}
static void reset_link(void){close_link();emit(obj("reset_link"));}
static void reconcile_link(uint64_t now) {
    if(connected&&!trusted()){event("Virtual central refused: open pairing on the simulator first, or use the saved central");reset_link();}
    if(connected&&subscribed&&trusted()){
        if(!core.s.connected)hm_core_open(&core,max_value,now);
    }else if(core.s.connected){hm_core_close(&core);hm_fragment_reset(&fragments);}
    observe(now);advertise(now);
}
static void apply_actions(hm_product_actions_t actions,uint64_t now) {
    char reply[513]={0};
    if(actions&HM_ACTION_PTT_UP){hm_core_press(&core,false,now,reply);send_control(reply,false);}
    if(actions&HM_ACTION_PTT_DOWN){hm_core_press(&core,true,now,reply);send_control(reply,false);}
    if(actions&HM_ACTION_PAIR_CLOSE){pair_deadline=0;if(connected&&!bonded())reset_link();}
    if(actions&HM_ACTION_PAIR_OPEN){hm_core_stop(&core,"pairing",reply);send_control(reply,false);pair_deadline=now+60000;hm_product_action_result(&product,HM_ACTION_PAIR_OPEN,true,now);event("Virtual pairing window open for 60 seconds; no physical passkey or OS bond is simulated");}
    if(actions&(HM_ACTION_UNPAIR|HM_ACTION_UNPAIR_AND_PAIR)){
        hm_product_actions_t remove=actions&(HM_ACTION_UNPAIR|HM_ACTION_UNPAIR_AND_PAIR);
        bool ok=save_settings(saved_volume,saved_theme,"",saved_spectrum);
        hm_product_action_result(&product,remove,ok,now);
        if(ok){saved_central[0]=0;pair_deadline=0;reset_link();event("Simulator virtual binding removed; physical device and macOS bonds unchanged");if(remove&HM_ACTION_UNPAIR_AND_PAIR)apply_actions(HM_ACTION_PAIR_OPEN,now);}
    }
    if(core.s.volume!=product.volume){hm_core_preview_volume(&core,product.volume,reply);send_control(reply,false);}
    core.remote_volume_busy=hm_product_volume_busy(&product);observe(now);advertise(now);
}
static void sync_slots(void) {
    if(ring_sid!=core.s.sid||ring_stream!=core.s.stream_id||core.s.direction!=HM_DIRECTION_SPEAKER){memset(slots,0,sizeof slots);ring_sid=core.s.sid;ring_stream=core.s.stream_id;}
    for(unsigned i=0;i<32;i++)if(slots[i].used&&slots[i].seq<core.s.next_seq)slots[i].used=false;
}
static void sync_spectrum_mode(void){
    if(!spectrum_mode_known||spectrum_mode!=product.spectrum_enabled){
        spectrum_mode=product.spectrum_enabled;spectrum_mode_known=true;
        analysis_count=0;analysis_at=0;memset(analysis_pcm,0,sizeof analysis_pcm);
        hm_spectrum_reset(&spectrum);memset(spectrum_target,0,sizeof spectrum_target);analyzed_revision=analysis_revision;
    }
}
static void feed_spectrum(const int16_t *pcm,unsigned samples,uint64_t now) {
    sync_spectrum_mode();if(!product.spectrum_enabled)return;
    if(analysis_sid!=core.s.sid||analysis_stream!=core.s.stream_id){analysis_count=0;memset(analysis_pcm,0,sizeof analysis_pcm);}
    analysis_sid=core.s.sid;analysis_stream=core.s.stream_id;
    memmove(analysis_pcm,analysis_pcm+samples,(256-samples)*sizeof(int16_t));memcpy(analysis_pcm+256-samples,pcm,samples*sizeof(int16_t));
    analysis_count=analysis_count+samples>256?256:analysis_count+samples;analysis_revision++;analysis_at=now;
}
static void receive_speaker(const uint8_t *wire,size_t n,uint64_t now) {
    if(!core.s.connected||!hm_core_audio_accept(&core,wire,n,now)){audio_rejected++;return;}
    audio_accepted++;observe(now);hm_product_received_audio(&product,&core.s,now);
    sync_slots();uint32_t seq=read32(wire+12);slot_t *slot=&slots[seq%32];
    if(slot->used&&slot->seq!=seq)core.stats.overflow++;
    slot->used=true;slot->seq=seq;
    (void)hm_adpcm_decode(wire+16,n-16,slot->pcm);
    unsigned count=0;for(unsigned i=0;i<32;i++)count+=slots[i].used;
    if(count>120/hm_core_frame_ms(&core)){while(count>60/hm_core_frame_ms(&core)){slot_t *old=NULL;for(unsigned i=0;i<32;i++)if(slots[i].used&&(!old||slots[i].seq<old->seq))old=&slots[i];if(!old)break;old->used=false;count--;core.stats.overflow++;}
        uint32_t next=UINT32_MAX;for(unsigned i=0;i<32;i++)if(slots[i].used&&slots[i].seq<next)next=slots[i].seq;if(next!=UINT32_MAX)hm_core_advance(&core,next,now);}
}
static void record_arrival(const cJSON *j,const uint8_t *wire,size_t n) {
    sync_arrival_scope();
    if(core.s.direction!=HM_DIRECTION_SPEAKER||n<16||read32(wire+4)!=core.s.sid||read32(wire+8)!=core.s.stream_id)return;
    cJSON *v=get(j,"arrival_ns");if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<=0||v->valuedouble>=18446744073709551616.0)return;
    uint64_t ns=(uint64_t)v->valuedouble;
    if(arrival_last_ns&&ns>=arrival_last_ns){double ms=(ns-arrival_last_ns)/1e6;arrival_count++;double d=ms-arrival_mean_ms;arrival_mean_ms+=d/(double)arrival_count;arrival_m2+=d*(ms-arrival_mean_ms);if(arrival_count==1||ms<arrival_min_ms)arrival_min_ms=ms;if(ms>arrival_max_ms)arrival_max_ms=ms;}
    arrival_last_ns=ns;
}
static void process_control(const uint8_t *wire,size_t n,uint64_t now) {
    if(!core.s.connected||!subscribed||!trusted()){input_errors++;event("Control received without authorized fully subscribed virtual link");return;}
    hm_fragment_result_t result=hm_fragment_push(&fragments,wire,n,now);
    if(result==HM_FRAGMENT_BAD){input_errors++;event("Invalid control fragment");return;}
    if(result!=HM_FRAGMENT_COMPLETE)return;
    const char *end=NULL;cJSON *request=cJSON_ParseWithLengthOpts((const char *)fragments.bytes,fragments.total,&end,false);
    const char *op=text_value(request,"op");bool hello=op&&!strcmp(op,"hello");cJSON_Delete(request);
    char reply[513]={0};hm_core_command(&core,(const char *)fragments.bytes,fragments.total,now,reply);
    cJSON *response=cJSON_Parse(reply);
    if(!cJSON_IsTrue(get(response,"ok"))){control_rejected++;const char *error=text_value(response,"error");if(error&&!strcmp(error,"bad_message"))input_errors++;}
    cJSON_Delete(response);
    if(hello&&core.s.audio_ready&&!bonded()){
        if(save_settings(saved_volume,saved_theme,central,saved_spectrum)){
            snprintf(saved_central,sizeof saved_central,"%s",central);pair_deadline=0;event("Virtual binding saved after accepted HELLO; this does not establish a physical security bond");
        }else{settings_fault=true;event("Virtual binding could not be saved; closing link");reset_link();return;}
    }
    send_control(reply,hello);observe(now);advertise(now);
}
static void process_line(char *line) {
    const char *end=NULL;cJSON *j=cJSON_ParseWithOpts(line,&end,true);
    if(!cJSON_IsObject(j)){input_errors++;cJSON_Delete(j);return;}
    const char *type=text_value(j,"type");uint64_t now=now_ms();
    if(!type){input_errors++;goto done;}
    if(!strcmp(type,"shutdown")){running=false;goto done;}
    if(!strcmp(type,"reset")){pair_deadline=0;reset_link();hm_product_init(&product,saved_volume,saved_theme,(hm_product_store_t){.store_volume=local_volume,.store_theme=store_theme,.store_spectrum=store_spectrum});hm_product_load_spectrum(&product,saved_spectrum);observe(now);advertise(now);event("Simulator session reset; virtual settings retained");goto done;}
    if(!strcmp(type,"bridge_state"))goto done;
    if(!strcmp(type,"link")){
        cJSON *con=get(j,"connected"),*sub=get(j,"subscribed"),*max=get(j,"max_value");const char *id=text_value(j,"central_id");
        if(!cJSON_IsBool(con)){input_errors++;goto done;}
        if(!cJSON_IsTrue(con)){close_link();reconcile_link(now);goto done;}
        if(!cJSON_IsBool(sub)||!id||!id[0]||strlen(id)>=sizeof central||!cJSON_IsNumber(max)||max->valuedouble<20||max->valuedouble>512||floor(max->valuedouble)!=max->valuedouble){input_errors++;goto done;}
        if(connected&&strcmp(central,id)){event("Second virtual central refused");input_errors++;goto done;}
        uint16_t next_max=max->valueint>244?244:(uint16_t)max->valueint;
        if(core.s.connected&&next_max!=max_value){hm_core_close(&core);hm_fragment_reset(&fragments);}
        connected=true;subscribed=cJSON_IsTrue(sub);max_value=next_max;snprintf(central,sizeof central,"%s",id);reconcile_link(now);goto done;
    }
    if(!strcmp(type,"button")){
        const char *key=text_value(j,"key"),*edge=text_value(j,"event");int k=key&&!strcmp(key,"up")?0:key&&!strcmp(key,"down")?1:key&&!strcmp(key,"ok")?2:-1;
        if(k<0||!edge||(strcmp(edge,"press")&&strcmp(edge,"release"))){input_errors++;goto done;}
        observe(now);hm_product_actions_t a=!strcmp(edge,"press")?hm_product_button(&product,(hm_product_key_t)k,HM_KEY_PRESS,now):hm_product_release_click(&product,(hm_product_key_t)k,now);
        apply_actions(a,now);goto done;
    }
    if(!strcmp(type,"ble_write")||!strcmp(type,"mic")){
        uint8_t wire[520];size_t n=unhex(get(j,"hex"),wire,sizeof wire);if(n==SIZE_MAX){input_errors++;goto done;}
        if(!strcmp(type,"mic")){
            static uint8_t pending[320]; static uint32_t pending_sid,pending_stream; static uint64_t pending_at; static bool half;
            if(n!=160){input_errors++;goto done;}mic_input++;uint8_t packet[100];
            {
                if(core.s.direction!=HM_DIRECTION_MIC||!core.first_credit||!core.s.physical_ptt) { half=false;mic_dropped++;goto done; }
                if(!half||pending_sid!=core.s.sid||pending_stream!=core.s.stream_id||now-pending_at>30) {
                    memcpy(pending,wire,160);pending_sid=core.s.sid;pending_stream=core.s.stream_id;pending_at=now;half=true;goto done;
                }
                memcpy(pending+160,wire,160);memcpy(wire,pending,320);half=false;
            }
            if(hm_core_mic_frame(&core,wire,now,packet)){hex_event("ble_notify","mic",packet,hm_core_audio_bytes(&core));core.transport_boot_submitted++;int16_t pcm[160];unsigned samples=hm_core_frame_samples(&core);memcpy(pcm,wire,samples*2);feed_spectrum(pcm,samples,now);}else mic_dropped++;
            goto done;
        }
        const char *channel=text_value(j,"channel");
        if(channel&&!strcmp(channel,"control"))process_control(wire,n,now);
        else if(channel&&!strcmp(channel,"speaker")){record_arrival(j,wire,n);receive_speaker(wire,n,now);}
        else if(channel&&!strcmp(channel,"app_flow")){if(!core.s.connected||!hm_core_on_flow(&core,wire,n,now))flow_rejected++;}
        else input_errors++;
        goto done;
    }
    input_errors++;
done:cJSON_Delete(j);
}
static void flush_pixels(lv_display_t *d,const lv_area_t *a,uint8_t *data) {
    uint16_t *p=(uint16_t *)data;
    for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)pixels[y*240+x]=*p++;
    lv_display_flush_ready(d);
}
static void frequency(char *out,size_t n,uint32_t hz){snprintf(out,n,"%lu.%03lu MHz",(unsigned long)(hz/1000000),(unsigned long)((hz%1000000)/1000));}
static void refresh_ui(uint64_t now) {
    hm_ui_snapshot_t ui={0};hm_snapshot_t s;hm_core_get_snapshot(&core,now,&s);observe(now);ui.p=product;ui.now_ms=now;ui.settings_fault=settings_fault;
    if(window_open(now))ui.pair_seconds=(uint8_t)((pair_deadline-now+999)/1000);
    if(s.direction==HM_DIRECTION_MIC)ui.tx_elapsed_s=(uint32_t)((now-core.stream_started_ms)/1000);
    bool active=s.stream_id&&(s.direction==HM_DIRECTION_SPEAKER||(s.direction==HM_DIRECTION_MIC&&s.physical_ptt));
    sync_spectrum_mode();
    bool fresh=product.spectrum_enabled&&active&&analysis_sid==s.sid&&analysis_stream==s.stream_id&&analysis_count>=256&&now>=analysis_at&&now-analysis_at<150;
    if(s.sid!=spectrum_sid||s.stream_id!=spectrum_stream||!active){hm_spectrum_reset(&spectrum);memset(spectrum_target,0,sizeof spectrum_target);spectrum_sid=s.sid;spectrum_stream=s.stream_id;}
    if(product.spectrum_enabled){
    if(fresh&&analyzed_revision!=analysis_revision)hm_spectrum_analyze(&spectrum,analysis_pcm,spectrum_target);
    else if(!fresh)memset(spectrum_target,0,sizeof spectrum_target);
    analyzed_revision=analysis_revision;uint8_t levels[16],peaks[16];hm_spectrum_step(&spectrum,spectrum_target,levels,peaks);
    for(unsigned i=0;i<16;i++){ui.spectrum[i]=(levels[i]*100u+127)/255;ui.peaks[i]=(peaks[i]*100u+127)/255;}
    }
    if(s.position.valid){snprintf(ui.position,sizeof ui.position,"%.5f, %.5f",(double)s.position.lat_e7/1e7,(double)s.position.lon_e7/1e7);
        if(s.position.distance_known){if(s.position.distance_m>=1000)snprintf(ui.distance,sizeof ui.distance,"%s%.1f km",s.position.approx?"约 ":"",s.position.distance_m/1000.0);else snprintf(ui.distance,sizeof ui.distance,"%s%lu m",s.position.approx?"约 ":"",(unsigned long)s.position.distance_m);}
        snprintf(ui.age,sizeof ui.age,"位置 %lu 秒前",(unsigned long)s.position.age_s);}
    if(s.tele.valid){char a[24],b[24];frequency(a,sizeof a,s.tele.frequency_a_hz);frequency(b,sizeof b,s.tele.frequency_b_hz);snprintf(ui.frequency,sizeof ui.frequency,"A %s\nB %s",a,b);}
    else if(s.rig.valid)frequency(ui.frequency,sizeof ui.frequency,s.rig.frequency_hz);
    if(s.rig.valid){snprintf(ui.grid,sizeof ui.grid,"%s",s.rig.grid);snprintf(ui.rig,sizeof ui.rig,"%s",s.rig.model);snprintf(ui.antenna,sizeof ui.antenna,"%s",s.rig.antenna);snprintf(ui.height,sizeof ui.height,"%lu m",(unsigned long)s.rig.height_m);if(!s.position.valid)snprintf(ui.age,sizeof ui.age,"资料 %lu 秒前",(unsigned long)s.rig.age_s);}
    hm_ui_set_phase(&ui);snprintf(ui.device_name,sizeof ui.device_name,"RF Link · SIM");snprintf(ui.firmware_version,sizeof ui.firmware_version,"simulator / UI v3");
    hm_ui_render(&ui);lv_obj_update_layout(lv_screen_active());lv_refr_now(display);
    if(first_frame||memcmp(pixels,previous_pixels,sizeof pixels)){hex_event("frame",NULL,(const uint8_t *)pixels,sizeof pixels);memcpy(previous_pixels,pixels,sizeof pixels);first_frame=false;}
}
static void characteristic(const char *channel,const char *value){cJSON *j=obj("characteristic");cJSON_AddStringToObject(j,"channel",channel);cJSON_AddStringToObject(j,"text",value);emit(j);}
static void update_characteristics(void){char value[513];hm_core_info(&core,value);if(strcmp(previous_info,value)){characteristic("info",value);strcpy(previous_info,value);}hm_core_status(&core,value);if(strcmp(previous_status,value)){characteristic("status",value);strcpy(previous_status,value);}}
static const char *direction(void){return core.s.direction==HM_DIRECTION_MIC?"mic":core.s.direction==HM_DIRECTION_SPEAKER?"speaker":"none";}
static void metrics(uint64_t now) {
    cJSON *j=obj("metrics");
#define NUM(k,v) cJSON_AddNumberToObject(j,k,(double)(v))
#define BOOL(k,v) cJSON_AddBoolToObject(j,k,v)
    NUM("uptime_ms",now);NUM("sid",core.s.sid);NUM("context_id",core.s.context_id);NUM("turn_id",core.s.turn_id);NUM("stream_id",core.s.stream_id);NUM("press_id",core.s.press_id);
    cJSON_AddStringToObject(j,"direction",direction());BOOL("connected",core.s.connected);BOOL("native_connected",connected);BOOL("subscribed",subscribed);BOOL("ready",core.s.ready);BOOL("audio_ready",core.s.audio_ready);BOOL("physical_ptt",core.s.physical_ptt);BOOL("pending_ptt",core.s.pending_ptt);BOOL("wait_release",core.s.wait_release);
    BOOL("muted",core.s.muted);BOOL("tx_allowed",core.s.tx_allowed);BOOL("talker_active",core.s.talker_active);BOOL("rejected",core.s.rejected);NUM("talker_uid",core.s.talker_uid);NUM("battery",core.s.battery);NUM("stats_stream",core.stats_stream);NUM("consume_ms",core.consume_ms);NUM("last_pcm_ms",core.last_pcm_ms);NUM("last_app_flow_ms",core.last_flow_ms);NUM("mic_flow_ms",core.mic_flow_ms);NUM("next_seq",core.s.next_seq);NUM("grant_end",core.s.grant_end);NUM("tx_seq",core.s.tx_seq);NUM("peer_next",core.peer_next);NUM("peer_end",core.peer_end);NUM("format",core.format);NUM("frame_ms",hm_core_frame_ms(&core));NUM("value_max",core.value_max);NUM("gatt_max",core.gatt_max);
    NUM("rx_packets",core.stats.rx_packets);NUM("tx_packets",core.stats.tx_packets);NUM("seq_missing",core.stats.seq_missing);NUM("duplicate",core.stats.duplicate);NUM("late",core.stats.late);NUM("overflow",core.stats.overflow);NUM("underrun",core.stats.underrun);NUM("io_errors",core.stats.io_errors);
    NUM("audio_accepted",audio_accepted);NUM("audio_rejected",audio_rejected);NUM("mic_input",mic_input);NUM("mic_dropped",mic_dropped);NUM("flow_rejected",flow_rejected);NUM("control_rejected",control_rejected);NUM("playback_frames",playback_frames);NUM("input_errors",input_errors);NUM("fragment_dropped",fragments.dropped);NUM("clock_overruns",clock_overruns);
    NUM("transport_boot_submitted",core.transport_boot_submitted);NUM("transport_boot_dropped",core.transport_boot_dropped);
    unsigned occupancy=0;for(unsigned i=0;i<32;i++)occupancy+=slots[i].used;NUM("buffer_occupancy",occupancy);NUM("buffer_ms",occupancy*hm_core_frame_ms(&core));NUM("core_mask",core.received_mask);
    cJSON_AddStringToObject(j,"rx_interval_scope","latest_speaker_stream");NUM("rx_arrival_sid",arrival_sid);NUM("rx_arrival_stream_id",arrival_stream);NUM("rx_arrival_intervals",arrival_count);NUM("rx_interval_mean_ms",arrival_mean_ms);NUM("rx_interval_min_ms",arrival_min_ms);NUM("rx_interval_max_ms",arrival_max_ms);NUM("rx_jitter_stddev_ms",arrival_count>1?sqrt(arrival_m2/(double)(arrival_count-1)):0);
    NUM("page",product.page);NUM("menu_index",product.menu_index);NUM("phone_index",product.phone_index);NUM("detail_page",product.detail_page);NUM("history_page",product.history_page);NUM("confirm_index",product.confirm_index);NUM("notice",product.notice);BOOL("spectrum_enabled",product.spectrum_enabled);NUM("volume",product.volume);NUM("saved_volume",product.saved_volume);cJSON_AddStringToObject(j,"theme",hm_product_palette(product.theme)->id);BOOL("volume_dirty",product.volume_dirty);BOOL("virtual_bonded",bonded());BOOL("pair_window",window_open(now));NUM("pair_seconds",window_open(now)?(pair_deadline-now+999)/1000:0);NUM("pair_state",product.pair_state);BOOL("settings_fault",settings_fault);
    cJSON_AddStringToObject(j,"security","virtual_binding_only");BOOL("physical_passkey_reproduced",false);BOOL("native_security_os_managed",true);cJSON_AddStringToObject(j,"virtualcentral",saved_central);cJSON_AddStringToObject(j,"network",core.s.network);cJSON_AddStringToObject(j,"callsign",core.s.callsign);cJSON_AddStringToObject(j,"server",core.s.server);cJSON_AddStringToObject(j,"talker_callsign",core.s.talker_callsign);cJSON_AddStringToObject(j,"stop_reason",core.s.stop_reason);
    cJSON *history=cJSON_AddArrayToObject(j,"history");for(unsigned i=0;i<product.history_count;i++){cJSON *h=cJSON_CreateObject();cJSON_AddNumberToObject(h,"uid",product.history[i].uid);cJSON_AddStringToObject(h,"callsign",product.history[i].callsign);cJSON_AddNumberToObject(h,"received_ms",(double)product.history[i].received_ms);cJSON_AddItemToArray(history,h);}
    emit(j);
#undef NUM
#undef BOOL
}
static void tick(uint64_t now) {
    char reply[513];for(unsigned i=0;i<4&&hm_core_tick(&core,now,reply);i++)send_control(reply,false);
    if(pair_deadline&&now>=pair_deadline){pair_deadline=0;if(!bonded()&&connected)reset_link();event("Virtual pairing window expired");}
    if(hm_fragment_expire(&fragments,now)){input_errors++;event("Control fragment assembly timeout");}
    observe(now);apply_actions(hm_product_tick(&product,now),now);
    static uint64_t playback_at; static unsigned playback_format;
    sync_slots();unsigned samples=hm_core_frame_samples(&core);
    if(playback_format!=core.format){playback_format=core.format;playback_at=now;}
    bool output_audio=now>=playback_at;
    uint32_t seq=0;bool consumed=output_audio&&hm_core_speaker_slot(&core,now,&seq);int16_t pcm[160]={0};
    if(consumed){slot_t *slot=&slots[seq%32];if(slot->used&&slot->seq==seq){memcpy(pcm,slot->pcm,samples*2);slot->used=false;}if(core.s.muted||!core.s.volume)memset(pcm,0,sizeof pcm);feed_spectrum(pcm,samples,now);}
    if(!playback_source_known||playback_sid!=core.s.sid||playback_stream!=core.s.stream_id||playback_direction!=core.s.direction){
        cJSON *boundary=obj("audio_boundary");cJSON_AddStringToObject(boundary,"source","processed");
        cJSON_AddNumberToObject(boundary,"sid",core.s.sid);cJSON_AddNumberToObject(boundary,"stream_id",core.s.stream_id);cJSON_AddStringToObject(boundary,"direction",direction());emit(boundary);
        playback_source_known=true;playback_sid=core.s.sid;playback_stream=core.s.stream_id;playback_direction=core.s.direction;
    }
    /* Matches codec writes: continuous 160-sample / 20ms slots, including idle silence. */
    if(output_audio){hex_event("playback",NULL,(const uint8_t *)pcm,samples*2);playback_frames++;unsigned frame_ms=hm_core_frame_ms(&core);playback_at=now>playback_at+frame_ms?now+frame_ms:playback_at+frame_ms;}
    uint64_t interval=core.s.direction!=HM_DIRECTION_NONE||core.s.pending_ptt?100:500;
    bool changed=core.s.sid!=last_flow_sid||core.s.stream_id!=last_flow_stream;
    if(changed){last_flow_sid=core.s.sid;last_flow_stream=core.s.stream_id;last_flow_next=0;}
    if(core.s.sid&&(changed||now-flow_at>=interval||core.s.next_seq-last_flow_next>=40/hm_core_frame_ms(&core)||core.s.press_id!=last_flow_press||core.s.physical_ptt!=last_flow_down)){uint8_t flow[24];if(hm_core_make_flow(&core,flow)){hex_event("ble_notify","flow",flow,sizeof flow);core.transport_boot_submitted++;}flow_at=now;last_flow_next=core.s.next_seq;last_flow_press=core.s.press_id;last_flow_down=core.s.physical_ptt;}
    advertise(now);update_characteristics();
    if(now>=ui_at){refresh_ui(now);ui_at=now+50;}
    if(now>=metrics_at){metrics(now);metrics_at=now+1000;}
}
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"Usage: passport_simulator /absolute/path/settings.json\n");return 2;}
    settings_path=argv[1];epoch_ms=absolute_ms();signal(SIGPIPE,SIG_IGN);load_settings();
    hm_core_init(&core,DEVICE_ID,saved_volume);hm_core_set_volume_store(&core,remote_volume,NULL);
    hm_product_init(&product,saved_volume,saved_theme,(hm_product_store_t){.store_volume=local_volume,.store_theme=store_theme,.store_spectrum=store_spectrum});hm_product_load_spectrum(&product,saved_spectrum);hm_spectrum_init(&spectrum);observe(0);
    lv_init();display=lv_display_create(240,320);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,draw_buffer,NULL,sizeof draw_buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush_pixels);hm_ui_create();
    int flags=fcntl(STDIN_FILENO,F_GETFL,0);if(flags>=0)fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK);
    event("Diagnostic simulator: actual Passport core/product/LVGL; BLE security, codec and ESP scheduler are not reproduced");
    advertise(0);update_characteristics();refresh_ui(0);metrics(0);ui_at=50;metrics_at=1000;
    static char input[INPUT_MAX+1];size_t used=0;bool discard=false;
    uint64_t due=now_ms()+10;last_tick=now_ms();
    while(running){
        uint64_t now=now_ms();
        if(now>=due){if(now>due+20)clock_overruns++;lv_tick_inc((uint32_t)(now-last_tick));last_tick=now;tick(now);due+=10;}
        unsigned lines=0;
        while(running&&lines<64){
            char *newline=memchr(input,'\n',used);if(!newline)break;size_t len=(size_t)(newline-input);
            input[len]=0;if(!discard){if(memchr(input,0,len)){input_errors++;}else process_line(input);}else discard=false;
            size_t take=len+1;memmove(input,input+take,used-take);used-=take;lines++;
        }
        if(!running)break;
        if(used==INPUT_MAX){input_errors++;used=0;discard=true;}
        now=now_ms();uint64_t wait=due>now?due-now:0;if(lines>=64)wait=0;
        fd_set rfds;FD_ZERO(&rfds);FD_SET(STDIN_FILENO,&rfds);struct timeval timeout={0,(suseconds_t)(wait*1000)};
        int ready=select(STDIN_FILENO+1,&rfds,NULL,NULL,&timeout);
        if(ready<0){if(errno==EINTR)continue;fprintf(stderr,"stdin select failed: %s\n",strerror(errno));break;}
        if(ready>0){ssize_t n=read(STDIN_FILENO,input+used,INPUT_MAX-used);if(n>0)used+=(size_t)n;else if(!n)break;else if(errno!=EAGAIN&&errno!=EINTR)break;}
    }
    return 0;
}
