#include "hm_core.h"
#include "cJSON.h"
#include "vectors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static hm_core_t c;
static char out[513];
static uint32_t rid;
static cJSON *request(const char *op,const char *params,uint64_t now) {
    char b[513]; snprintf(b,sizeof b,"{\"v\":1,\"id\":%u,\"sid\":%u,\"op\":\"%s\",\"p\":%s}",++rid,strcmp(op,"hello")?c.s.sid:0,op,params);
    assert(hm_core_command(&c,b,strlen(b),now,out)); cJSON *r=cJSON_Parse(out); assert(r); return r;
}
static void ok(const char *op,const char *p,uint64_t now) { cJSON *r=request(op,p,now); if(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok"))) fprintf(stderr,"unexpected %s: %s\n",op,out); assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok"))); cJSON_Delete(r); }
static void setup(void) { hm_core_init(&c,"b56c4ce3-b6f5-44f2-b4f5-fb47f8b2d1a9",90); hm_core_open(&c,244,0); rid=0; ok("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":244}",0); }

static void error(const char *op,const char *p,uint64_t now,const char *e) { cJSON *r=request(op,p,now); assert(cJSON_IsFalse(cJSON_GetObjectItem(r,"ok"))); assert(!strcmp(cJSON_GetObjectItem(r,"error")->valuestring,e)); cJSON_Delete(r); }
static void radio(uint64_t now) { ok("radio.update","{\"context_id\":1,\"network\":\"online\",\"callsign\":\"TEST-1\",\"server\":\"测试\",\"phase\":\"idle\",\"muted\":false,\"tx_allowed\":true}",now); }
static void test_adpcm_only_capabilities(void) {
    setup();char info[513];hm_core_info(&c,info);
    cJSON *j=cJSON_Parse(info),*formats=cJSON_GetObjectItem(j,"formats");
    assert(cJSON_GetArraySize(formats)==1&&cJSON_GetArrayItem(formats,0)->valueint==2);
    assert(cJSON_GetObjectItem(j,"frame_samples")->valueint==160&&cJSON_GetObjectItem(j,"audio_bytes")->valueint==100);
    cJSON_Delete(j);
    error("hello","{\"major\":1,\"minor\":0,\"format\":1,\"write_max\":244}",1,"unsupported_format");
}
static unsigned audio_report_calls;
static bool audio_report(const char *section,char json[400]) {
    audio_report_calls++;snprintf(json,400,"{\"section\":\"%s\",\"count\":%u}",section,audio_report_calls);return true;
}
static void test_audio_diagnostics_dispatch(void) {
    setup();error("audio.stats","{}",0,"unsupported_operation");c.audio_report=audio_report;audio_report_calls=0;
    ok("audio.stats","{}",1);assert(strstr(out,"\"section\":\"rx\"")&&audio_report_calls==1);
    char cached[513],wire[513];strcpy(cached,out);
    snprintf(wire,sizeof wire,"{\"v\":1,\"id\":%u,\"sid\":%u,\"op\":\"audio.stats\",\"p\":{}}",rid,c.s.sid);
    assert(hm_core_command(&c,wire,strlen(wire),2,out)&&!strcmp(out,cached)&&audio_report_calls==1);
    ok("audio.stats","{\"section\":\"playback\"}",3);assert(strstr(out,"\"section\":\"playback\"")&&audio_report_calls==2);
    error("audio.stats","{\"section\":\"unknown\"}",4,"bad_message");assert(audio_report_calls==2);
}
static void talker(uint64_t now) { ok("talker.update","{\"context_id\":1,\"turn_id\":2,\"uid\":2,\"callsign\":\"TEST-2\",\"active\":true}",now); }
static void put32(unsigned char *p,uint32_t n) { for(int i=0;i<4;i++) p[i]=(unsigned char)(n>>(8*i)); }
static void flow(unsigned char f[24],uint32_t stream,uint32_t n,uint32_t end) { memset(f,0,24); f[0]=1; put32(f+4,c.s.sid); put32(f+8,stream); put32(f+12,n); put32(f+16,end); }
static void test_hello_cache(void) {
    setup(); assert(c.s.sid && c.s.audio_ready && !c.s.ready); uint32_t sid=c.s.sid;
    const char *hello="{\"v\":1,\"id\":1,\"sid\":0,\"op\":\"hello\",\"p\":{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":244}}";
    assert(hm_core_command(&c,hello,strlen(hello),1,out)); assert(c.s.sid==sid);
    radio(2); assert(c.s.ready); for(int i=0;i<20;i++) ok("volume.set","{\"value\":85}",3);
    assert(hm_core_command(&c,hello,strlen(hello),4,out)); assert(c.s.sid==sid && c.s.volume==85);
    const char *old="{\"v\":1,\"id\":2,\"sid\":0,\"op\":\"volume.set\",\"p\":{\"value\":1}}";
    char b[513]; snprintf(b,sizeof b,"{\"v\":1,\"id\":2,\"sid\":%u,\"op\":\"volume.set\",\"p\":{\"value\":1}}",sid);
    (void)old; hm_core_command(&c,b,strlen(b),5,out); assert(strstr(out,"stale_request")); assert(c.s.volume==85);
    ok("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":244}",6); assert(c.s.sid!=sid && !c.s.ready);
    hm_core_command(&c,hello,strlen(hello),7,out); assert(strstr(out,"stale_request"));
    hm_core_init(&c,"id",90); hm_core_open(&c,99,0); rid=0; error("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":244}",0,"mtu_too_small");
}
static void test_ptt(void) {
    setup(); radio(0); assert(hm_core_press(&c,true,1,out)); assert(strstr(out,"\"down\":true"));
    ok("stream.start","{\"stream_id\":1,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",2);
    unsigned char f[24],pcm[320]={0},a[100]; assert(!hm_core_mic_frame(&c,pcm,2,a));
    flow(f,1,0,32); assert(hm_core_on_flow(&c,f,24,3)); assert(hm_core_mic_frame(&c,pcm,3,a)); assert(a[1]==1);
    assert(hm_core_press(&c,false,4,out)); assert(c.s.direction==HM_DIRECTION_NONE);
    error("stream.start","{\"stream_id\":2,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",5,"ptt_released");
    hm_core_press(&c,true,6,out); ok("ptt.reject","{\"press_id\":2,\"reason\":\"busy\"}",7);
    error("stream.start","{\"stream_id\":3,\"direction\":\"mic\",\"press_id\":2,\"turn_id\":0}",8,"ptt_released");
    hm_core_press(&c,false,9,out); hm_core_press(&c,true,10,out); hm_core_tick(&c,1010,out); assert(c.s.wait_release);
    hm_core_press(&c,false,1011,out); hm_core_press(&c,true,1012,out);
    ok("stream.start","{\"stream_id\":4,\"direction\":\"mic\",\"press_id\":4,\"turn_id\":0}",1013);
    hm_core_tick(&c,1313,out); assert(c.s.direction==HM_DIRECTION_NONE);
    setup(); radio(0); hm_core_press(&c,true,1,out); ok("stream.start","{\"stream_id\":1,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",2);
    flow(f,1,0,32); hm_core_on_flow(&c,f,24,3); hm_core_tick(&c,503,out); assert(!c.s.stream_id && c.s.wait_release);
}
static void test_metadata(void) {
    setup(); radio(0); talker(0);
    ok("station.position","{\"context_id\":1,\"turn_id\":2,\"uid\":2,\"callsign\":\"TEST-2\",\"source\":\"rig_grid\",\"lat_e7\":-899791667,\"lon_e7\":-1799583333,\"accuracy_m\":null,\"age_s\":5,\"valid_for_ms\":295000,\"distance_m\":0,\"ref_age_s\":59,\"approx\":true}",0);
    assert(c.s.position.valid && c.s.position.distance_known && !c.s.position.distance_m && c.s.position.lat_e7<0);
    hm_snapshot_t s; hm_core_get_snapshot(&c,1000,&s); assert(s.position.valid && !s.position.distance_known && s.position.age_s==6);
    ok("station.tele","{\"context_id\":1,\"turn_id\":2,\"uid\":2,\"callsign\":\"TEST-2\",\"frequency_a_hz\":145500000,\"frequency_b_hz\":433500000,\"age_s\":299,\"valid_for_ms\":1000}",1000);
    assert(c.s.tele.valid); hm_core_tick(&c,2000,out); assert(!c.s.tele.valid);
    ok("station.tele","{\"context_id\":1,\"turn_id\":2,\"uid\":2,\"callsign\":\"TEST-2\",\"frequency_a_hz\":145500000,\"frequency_b_hz\":433500000,\"age_s\":299,\"valid_for_ms\":1000}",2000); assert(!c.s.tele.valid);
    ok("station.tele","{\"valid_for_ms\":1000,\"age_s\":299,\"frequency_b_hz\":433500000,\"frequency_a_hz\":145500000,\"callsign\":\"TEST-2\",\"uid\":2,\"turn_id\":2,\"context_id\":1}",2000); assert(!c.s.tele.valid);
    error("talker.update","{\"context_id\":1,\"turn_id\":1,\"uid\":2,\"callsign\":\"TEST-2\",\"active\":true}",2001,"stale_talker");
    ok("talker.update","{\"context_id\":1,\"turn_id\":2,\"uid\":2,\"callsign\":\"TEST-2\",\"active\":false}",2002); assert(!c.s.position.valid);
    error("talker.update","{\"context_id\":1,\"turn_id\":2,\"uid\":2,\"callsign\":\"TEST-2\",\"active\":true}",2003,"stale_talker");
}
static void test_audio(void) {
    setup(); radio(0); talker(0); ok("stream.start","{\"stream_id\":1,\"direction\":\"speaker\",\"press_id\":0,\"turn_id\":2}",0);
    unsigned char a[100],f[24]; memcpy(a,vector_audio,100); put32(a+4,c.s.sid);
    assert(hm_core_make_flow(&c,f) && f[16]==32); assert(hm_core_audio_accept(&c,a,100,1));
    assert(!hm_core_audio_accept(&c,a,100,2)); uint32_t slot; assert(hm_core_speaker_slot(&c,61,&slot) && slot==0); assert(c.s.next_seq==1);
    put32(a+12,4); a[1]=0; assert(hm_core_audio_accept(&c,a,100,62)); while(hm_core_speaker_slot(&c,141,&slot)) {} assert(c.s.next_seq==5);
    put32(a+12,20); a[1]=1; assert(hm_core_audio_accept(&c,a,100,142)); assert(c.s.next_seq==20);
    assert(!hm_core_audio_accept(&c,a,99,143)); a[1]=128; assert(!hm_core_audio_accept(&c,a,100,143));
    flow(f,0,0,0); assert(hm_core_on_flow(&c,f,24,200)); f[2]=1; assert(!hm_core_on_flow(&c,f,24,300));
    hm_core_tick(&c,442,out); assert(!c.s.stream_id); hm_core_tick(&c,3200,out); assert(!c.s.sid && !c.s.ready);
}
static void speaker_setup(void) {
    setup(); radio(0); talker(0);
    ok("stream.start","{\"stream_id\":1,\"direction\":\"speaker\",\"press_id\":0,\"turn_id\":2}",0);
}
static void accept_audio(uint32_t seq,bool discontinuity,uint64_t now) {
    unsigned char a[100]; memcpy(a,vector_audio,sizeof a);
    put32(a+4,c.s.sid); put32(a+8,c.s.stream_id); put32(a+12,seq); a[1]=discontinuity?1:0;
    assert(hm_core_audio_accept(&c,a,sizeof a,now));
}
static void test_speaker_clock_recovery(void) {
    uint32_t slot;
    speaker_setup(); accept_audio(0,true,1);
    assert(c.consume_ms==61 && !hm_core_speaker_slot(&c,60,&slot));
    accept_audio(6,true,51); /* Frequent flags must not defer initial playback. */
    assert(c.consume_ms==61 && c.s.next_seq==6 && c.s.grant_end==38);
    assert(hm_core_speaker_slot(&c,61,&slot) && slot==6 && !c.stats.underrun);
    accept_audio(7,true,65); /* A flag at the current cursor preserves cadence. */
    assert(c.consume_ms==81 && !hm_core_speaker_slot(&c,80,&slot));
    assert(hm_core_speaker_slot(&c,81,&slot) && slot==7);
    accept_audio(12,true,101); /* A jump discards stale timing debt. */
    assert(c.consume_ms==101 && c.s.next_seq==12);
    assert(hm_core_speaker_slot(&c,101,&slot) && slot==12);
    assert(!hm_core_speaker_slot(&c,101,&slot));
    for(uint32_t seq=18;seq<=30;seq+=6) {
        uint64_t now=101+(seq-12)/6*50;
        accept_audio(seq,true,now);
        assert(hm_core_speaker_slot(&c,now,&slot) && slot==seq);
        assert(!hm_core_speaker_slot(&c,now,&slot));
    }
    unsigned char a[100]; memcpy(a,vector_audio,sizeof a); put32(a+4,c.s.sid); put32(a+12,29);
    assert(!hm_core_audio_accept(&c,a,sizeof a,252) && c.s.next_seq==31);
    ok("stream.stop","{\"stream_id\":1,\"reason\":\"remote_end\"}",253);
    ok("stream.start","{\"stream_id\":2,\"direction\":\"speaker\",\"press_id\":0,\"turn_id\":2}",254);
    accept_audio(0,true,255); assert(c.consume_ms==315);
    assert(!hm_core_speaker_slot(&c,314,&slot));
    assert(hm_core_speaker_slot(&c,315,&slot) && slot==0);
    hm_core_tick(&c,555,out); assert(!c.s.stream_id); /* Timeout unchanged. */
}
static void test_speaker_trim_clock(void) {
    uint32_t slot;
    speaker_setup();
    for(uint32_t seq=0;seq<7;seq++) accept_audio(seq,seq==0,1+seq);
    assert(c.stats.overflow==4 && c.s.next_seq==4 && c.consume_ms==61);
    assert(!hm_core_speaker_slot(&c,60,&slot));
    assert(hm_core_speaker_slot(&c,61,&slot) && slot==4);
    speaker_setup(); accept_audio(0,true,1);
    for(uint32_t seq=1;seq<7;seq++) accept_audio(seq,false,100);
    assert(c.s.next_seq==4 && c.consume_ms==100);
    assert(hm_core_speaker_slot(&c,100,&slot) && slot==4);
    assert(!hm_core_speaker_slot(&c,100,&slot));
}
static void test_adpcm_negotiation(void) {
    setup();
    ok("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":100}",1);
    cJSON *reply=cJSON_Parse(out), *r=cJSON_GetObjectItem(reply,"r");
    assert(cJSON_GetObjectItem(r,"format")->valueint==2);
    assert(cJSON_GetObjectItem(r,"minor")->valueint==1);
    assert(cJSON_GetObjectItem(r,"samples")->valueint==160);
    cJSON_Delete(reply);
    error("hello","{\"major\":1,\"minor\":0,\"format\":2,\"write_max\":244}",2,"unsupported_format");
    error("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":99}",3,"mtu_too_small");
    error("hello","{\"major\":1,\"minor\":1,\"format\":3,\"write_max\":244}",4,"unsupported_format");
}
static void test_adpcm_streams(void) {
    unsigned char a[100]={1,1,2,160}, f[24], pcm[320]={0}; uint32_t slot;
    setup(); ok("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":100}",0);
    radio(0); talker(0);
    ok("stream.start","{\"stream_id\":1,\"direction\":\"speaker\",\"press_id\":0,\"turn_id\":2}",0);
    put32(a+4,c.s.sid); put32(a+8,1);
    unsigned char legacy[176]={1,1,1,80};assert(!hm_core_audio_accept(&c,legacy,176,1)); /* PCM removed. */
    a[2]=1; assert(!hm_core_audio_accept(&c,a,100,1)); a[2]=2;
    a[18]=89; assert(!hm_core_audio_accept(&c,a,100,1)); a[18]=0;
    a[19]=1; assert(!hm_core_audio_accept(&c,a,100,1)); a[19]=0;
    a[99]=0x10; assert(!hm_core_audio_accept(&c,a,100,1)); a[99]=0;
    assert(!c.first_pcm && !c.stats.rx_packets);
    assert(hm_core_audio_accept(&c,a,100,1));
    assert(!hm_core_audio_accept(&c,a,100,2)); /* Duplicate. */
    assert(!hm_core_speaker_slot(&c,60,&slot));
    assert(hm_core_speaker_slot(&c,61,&slot)&&slot==0&&c.consume_ms==81);
    put32(a+12,4); assert(hm_core_audio_accept(&c,a,100,70));
    assert(c.consume_ms==81 && c.s.next_seq==4);
    assert(hm_core_speaker_slot(&c,81,&slot)&&slot==4);
    assert(hm_core_speaker_slot(&c,101,&slot)&&slot==5&&c.stats.underrun==1);
    ok("stop","{\"reason\":\"user_stop\"}",102);
    ok("stream.start","{\"stream_id\":2,\"direction\":\"speaker\",\"press_id\":0,\"turn_id\":2}",103);
    put32(a+8,2); a[1]=0;
    for(unsigned i=0;i<7;i++) { a[1]=i==0; put32(a+12,i); assert(hm_core_audio_accept(&c,a,100,104+i)); }
    assert(c.s.next_seq==4 && c.s.grant_end==36 && c.consume_ms==164);
    assert(hm_core_speaker_slot(&c,164,&slot)&&slot==4);
    ok("stop","{\"reason\":\"user_stop\"}",165);
    hm_core_press(&c,true,166,out);
    ok("stream.start","{\"stream_id\":3,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",167);
    assert(!hm_core_mic_frame(&c,pcm,167,a));
    flow(f,3,0,32); assert(hm_core_on_flow(&c,f,24,168));
    assert(hm_core_mic_frame(&c,pcm,168,a)&&a[2]==2&&a[3]==160&&a[1]==1);
    for(unsigned i=16;i<100;i++) assert(a[i]==0);
    assert(hm_core_mic_frame(&c,pcm,188,a)&&a[12]==1&&a[1]==0);
    assert(hm_core_mic_frame(&c,pcm,248,a)&&a[12]==4&&a[1]==1);
    hm_core_press(&c,false,249,out); assert(!hm_core_mic_frame(&c,pcm,250,a));
    ok("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":244}",251);
    assert(c.format==2&&hm_core_frame_ms(&c)==20&&hm_core_audio_bytes(&c)==100);
}
static void test_safety_boundaries(void) {
    setup(); radio(0); hm_core_press(&c,true,1,out);
    ok("stream.start","{\"stream_id\":1,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",2);
    error("stream.start","{\"stream_id\":1,\"direction\":\"mic\",\"press_id\":99,\"turn_id\":0}",3,"stream_conflict");
    unsigned char f[24],a[100],pcm[320]={0}; flow(f,1,0,32); assert(hm_core_on_flow(&c,f,24,3));
    for(int i=0;i<32;i++) assert(hm_core_mic_frame(&c,pcm,3+i*10,a));
    assert(!hm_core_mic_frame(&c,pcm,323,a) && c.s.tx_seq==33);
    flow(f,1,2,34); assert(hm_core_on_flow(&c,f,24,333)); assert(hm_core_mic_frame(&c,pcm,333,a) && a[1]==1 && a[12]==33);
    flow(f,1,1,33); assert(!hm_core_on_flow(&c,f,24,334));
    hm_core_set_audio_available(&c,false); assert(!c.s.audio_ready && !c.s.ready && !c.s.stream_id);
    radio(334); assert(!c.s.ready);
    error("stream.start","{\"stream_id\":2,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",335,"audio_error");
    error("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":244}",336,"audio_error");
    setup(); radio(0); hm_core_press(&c,true,1,out); hm_core_tick(&c,1001,out);
    error("stream.start","{\"stream_id\":1,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",1002,"ptt_released");
    setup(); radio(0); talker(0); ok("stream.start","{\"stream_id\":1,\"direction\":\"speaker\",\"press_id\":0,\"turn_id\":2}",0);
    memcpy(a,vector_audio,100); put32(a+4,c.s.sid); assert(hm_core_audio_accept(&c,a,100,1));
    assert(!hm_core_audio_accept(&c,a,100,301)); assert(!c.s.stream_id);
}
static void test_cutoff_and_stop(void) {
    setup(); radio(0); hm_core_press(&c,true,1,out);
    ok("stop","{\"reason\":\"user_stop\"}",2);
    error("stream.start","{\"stream_id\":1,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",3,"ptt_released");
    setup(); radio(0); hm_core_press(&c,true,1,out);
    ok("stream.start","{\"stream_id\":1,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",2);
    unsigned char f[24]; flow(f,1,0,32);
    for(uint64_t now=3;now<60002;now+=100) assert(hm_core_on_flow(&c,f,24,now));
    hm_core_tick(&c,60002,out); assert(!c.s.stream_id && c.s.wait_release);
    ok("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":244}",60003);
    assert(!c.stats_stream && !c.stats.rx_packets && !c.stats.late);
}
static void test_sample_clock_and_first_frame(void) {
    setup(); radio(0); hm_core_press(&c,true,1,out);
    ok("stream.start","{\"stream_id\":1,\"direction\":\"mic\",\"press_id\":1,\"turn_id\":0}",2);
    unsigned char f[24],a[100],pcm[320]={0}; flow(f,1,0,32); assert(hm_core_on_flow(&c,f,24,3));
    assert(hm_core_mic_frame(&c,pcm,3,a) && a[12]==0);
    assert(hm_core_mic_frame(&c,pcm,103,a) && a[12]==5 && a[1]==1);
    setup(); radio(0); talker(0); ok("stream.start","{\"stream_id\":1,\"direction\":\"speaker\",\"press_id\":0,\"turn_id\":2}",0);
    memcpy(a,vector_audio,100); put32(a+4,c.s.sid); a[1]=0; assert(!hm_core_audio_accept(&c,a,100,1));
}
static void test_public_metadata(void) {
    for(unsigned group=0;group<2;group++) {
        setup(); radio(0);
        if(group) ok("radio.update","{\"context_id\":4294967295,\"network\":\"online\",\"callsign\":\"TEST-1\",\"server\":\"测试\",\"phase\":\"rx\",\"muted\":false,\"tx_allowed\":true}",0);
        for(unsigned i=0;i<4;i++) {
            cJSON *j=cJSON_Parse(group?vector_station_metadata_boundary[i]:vector_station_metadata[i]); assert(j);
            cJSON_SetNumberValue(cJSON_GetObjectItem(j,"sid"),c.s.sid);
            cJSON_SetNumberValue(cJSON_GetObjectItem(j,"id"),++rid);
            char *wire=cJSON_PrintUnformatted(j); assert(wire); assert(strlen(wire)<=512);
            hm_core_command(&c,wire,strlen(wire),1,out); if(!strstr(out,"\"ok\":true")) fprintf(stderr,"vector: %s\n",out); assert(strstr(out,"\"ok\":true"));
            cJSON_free(wire); cJSON_Delete(j);
        }
        assert(c.s.tele.valid&&c.s.rig.valid&&c.s.position.valid&&c.s.position.distance_known);
        assert(c.s.position.lon_e7==-1799583333);
    }
}
static unsigned saved_count;
static bool save_volume(uint8_t value,void *ctx) { saved_count++; return value!=*(uint8_t *)ctx; }
static void test_volume_storage(void) {
    setup(); uint8_t fail=70; saved_count=0; hm_core_set_volume_store(&c,save_volume,&fail);
    error("volume.set","{\"value\":70}",1,"storage_error"); assert(c.s.volume==90&&saved_count==1);
    ok("volume.set","{\"value\":80}",2); assert(c.s.volume==80&&saved_count==2);
    assert(!hm_core_set_volume(&c,70,out)&&c.s.volume==80); assert(hm_core_set_volume(&c,0,out)&&c.s.volume==0);
    hm_core_press(&c,true,3,out); ok("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":244}",4);
    assert(!c.s.stream_id&&!c.s.ready);
    error("hello","{\"major\":2,\"minor\":1,\"format\":2,\"write_max\":244}",5,"unsupported_version"); assert(!c.s.sid);
}
static void test_network_stops_speaker(void) {
    setup(); radio(0); talker(0); ok("stream.start","{\"stream_id\":1,\"direction\":\"speaker\",\"press_id\":0,\"turn_id\":2}",0);
    ok("radio.update","{\"context_id\":1,\"network\":\"offline\",\"callsign\":\"TEST-1\",\"server\":\"测试\",\"phase\":\"idle\",\"muted\":false,\"tx_allowed\":false}",1);
    assert(!c.s.stream_id && !c.s.tx_allowed);
}
static void test_transport_stats_cache(void) {
    setup(); c.transport_boot_submitted=21; c.transport_boot_dropped=3;
    cJSON *reply=request("stats.get","{}",1), *r=cJSON_GetObjectItem(reply,"r");
    cJSON *submitted=cJSON_GetObjectItem(r,"transport_boot_submitted"), *dropped=cJSON_GetObjectItem(r,"transport_boot_dropped");
    assert(cJSON_IsNumber(submitted)&&submitted->valuedouble==21);
    assert(cJSON_IsNumber(dropped)&&dropped->valuedouble==3);
    assert(cJSON_GetObjectItem(r,"tx_packets")->valuedouble==0); cJSON_Delete(reply);
    char cached[513],wire[513]; strcpy(cached,out);
    snprintf(wire,sizeof wire,"{\"v\":1,\"id\":%u,\"sid\":%u,\"op\":\"stats.get\",\"p\":{}}",rid,c.s.sid);
    c.transport_boot_submitted=40; c.transport_boot_dropped=7;
    assert(hm_core_command(&c,wire,strlen(wire),2,out)&&!strcmp(out,cached));
    reply=request("stats.get","{}",3); r=cJSON_GetObjectItem(reply,"r");
    assert(cJSON_GetObjectItem(r,"transport_boot_submitted")->valuedouble==40);
    assert(cJSON_GetObjectItem(r,"transport_boot_dropped")->valuedouble==7); cJSON_Delete(reply);
    ok("hello","{\"major\":1,\"minor\":1,\"format\":2,\"write_max\":244}",4);
    assert(c.transport_boot_submitted==40&&c.transport_boot_dropped==7);
}
static void test_volume_busy_cache(void) {
    setup();radio(0);ok("volume.set","{\"value\":80}",1);
    char accepted[513],wire[513];strcpy(accepted,out);
    snprintf(wire,sizeof wire,"{\"v\":1,\"id\":%u,\"sid\":%u,\"op\":\"volume.set\",\"p\":{\"value\":80}}",rid,c.s.sid);
    c.remote_volume_busy=true;
    assert(hm_core_command(&c,wire,strlen(wire),2,out)&&!strcmp(out,accepted));
    error("volume.set","{\"value\":40}",3,"busy");assert(c.s.volume==80);
    c.remote_volume_busy=false;ok("volume.set","{\"value\":40}",4);assert(c.s.volume==40);
}
static void test_bad_json(void) {
    setup(); const char *bad[]={"{\"v\":1,\"v\":1}","{}{}","{\"v\":1.5}","{\"p\":\"\\u0000\"}","{\"p\":\"\\n\"}"};
    for(size_t i=0;i<sizeof bad/sizeof *bad;i++) { hm_core_command(&c,bad[i],strlen(bad[i]),1,out); assert(strstr(out,"bad_message")); }
}
static void test_local_ptt_outcome(void) {
    setup();radio(0);hm_core_press(&c,true,1,out);
    ok("ptt.reject","{\"press_id\":1,\"reason\":\"busy\"}",2);assert(!strcmp(c.s.stop_reason,"busy"));
    error("ptt.reject","{\"press_id\":1,\"reason\":42}",3,"bad_message");assert(!strcmp(c.s.stop_reason,"busy"));
    ok("ptt.reject","{\"press_id\":99,\"reason\":\"unknown\"}",4);assert(!strcmp(c.s.stop_reason,"busy"));
    hm_core_status(&c,out);assert(!strstr(out,"stop_reason"));
    hm_core_press(&c,false,5,out);assert(!c.s.stop_reason[0]);
    hm_core_press(&c,true,6,out);assert(!c.s.stop_reason[0]);hm_core_tick(&c,1006,out);assert(!strcmp(c.s.stop_reason,"timeout"));
    hm_core_press(&c,false,1007,out);assert(!c.s.stop_reason[0]&&!c.s.wait_release&&!c.s.rejected);
}
int main(void) {
    test_audio_diagnostics_dispatch();
    test_adpcm_only_capabilities();
    test_adpcm_negotiation(); test_adpcm_streams();
    assert(sizeof(vector_audio)==100 && sizeof(vector_app_flow_mic_credit)==24 && sizeof(vector_device_flow_ptt_down)==24);
    test_hello_cache(); test_ptt(); test_metadata(); test_audio(); test_speaker_clock_recovery(); test_speaker_trim_clock(); test_safety_boundaries(); test_cutoff_and_stop(); test_public_metadata(); test_sample_clock_and_first_frame(); test_volume_storage(); test_network_stops_speaker(); test_transport_stats_cache(); test_volume_busy_cache(); test_bad_json(); test_local_ptt_outcome();
    puts("handmic core tests passed: replay, PTT, metadata TTL, ADPCM, credit, leases, JSON"); return 0;
}
