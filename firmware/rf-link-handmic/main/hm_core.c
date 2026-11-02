#include "hm_core.h"
#include "hm_adpcm.h"
#include "cJSON.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned hm_core_frame_ms(const hm_core_t *c) { (void)c;return 20; }
unsigned hm_core_frame_samples(const hm_core_t *c) { (void)c;return 160; }
size_t hm_core_audio_bytes(const hm_core_t *c) { (void)c;return HM_AUDIO_BYTES; }

static cJSON *get(const cJSON *o,const char *k) { return cJSON_GetObjectItemCaseSensitive(o,k); }
static bool u32(const cJSON *v,uint32_t *n) { if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>UINT32_MAX||floor(v->valuedouble)!=v->valuedouble) return false; *n=(uint32_t)v->valuedouble; return true; }
static bool number(const cJSON *p,const char *k,uint32_t *n) { return u32(get(p,k),n); }
static bool boolean(const cJSON *p,const char *k,bool *v) { cJSON *j=get(p,k); if(!cJSON_IsBool(j)) return false; *v=cJSON_IsTrue(j); return true; }
static bool text(const cJSON *p,const char *k,char *v,size_t max) { cJSON *j=get(p,k); if(!cJSON_IsString(j)||strlen(j->valuestring)>max) return false; strcpy(v,j->valuestring); return true; }
static bool callsign(const char *s,bool empty) { if(!*s) return empty; for(;*s;s++) if(!(*s>='A'&&*s<='Z')&&!(*s>='0'&&*s<='9')&&*s!='-'&&*s!='/') return false; return true; }
static bool utf8(const unsigned char *s) {
    while(*s) { unsigned n; uint32_t cp; if(*s<0x80) { if(*s<32||*s==127) return false; s++; continue; }
        if(*s>=0xc2&&*s<=0xdf) { n=1; cp=*s&31; } else if(*s>=0xe0&&*s<=0xef) { n=2; cp=*s&15; } else if(*s>=0xf0&&*s<=0xf4) { n=3; cp=*s&7; } else return false;
        unsigned count=n; s++; while(n--) { if((*s&0xc0)!=0x80) return false; cp=(cp<<6)|(*s++&63); }
        if((count==2&&cp<0x800)||(count==3&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)) return false;
    } return true;
}
static bool tree(const cJSON *j,unsigned depth,const char *key) {
    if(depth>6) return false;
    if(cJSON_IsString(j)) return utf8((const unsigned char *)j->valuestring);
    if(cJSON_IsNumber(j)) { double d=j->valuedouble; bool signed_coord=key&&(!strcmp(key,"lat_e7")||!strcmp(key,"lon_e7")); return isfinite(d)&&floor(d)==d&&d<=(double)UINT32_MAX&&d>=(signed_coord?INT32_MIN:0); }
    if(cJSON_IsObject(j)||cJSON_IsArray(j)) { for(cJSON *v=j->child;v;v=v->next) { if(cJSON_IsObject(j)) { if(!v->string||!utf8((unsigned char *)v->string)) return false; for(cJSON *b=v->next;b;b=b->next) if(!strcmp(v->string,b->string)) return false; } if(!tree(v,depth+1,v->string)) return false; } return true; }
    return cJSON_IsBool(j)||cJSON_IsNull(j);
}
static bool emit(cJSON *j,char out[513]) { bool ok=cJSON_PrintPreallocated(j,out,513,false); if(!ok) strcpy(out,"{\"v\":1,\"id\":0,\"sid\":0,\"ok\":false,\"error\":\"bad_message\"}"); cJSON_Delete(j); return ok; }
static cJSON *object(void) { return cJSON_CreateObject(); }
static void num(cJSON *j,const char *k,uint32_t n) { cJSON_AddNumberToObject(j,k,n); }
static void str(cJSON *j,const char *k,const char *s) { cJSON_AddStringToObject(j,k,s); }
static void flag(cJSON *j,const char *k,bool b) { cJSON_AddBoolToObject(j,k,b); }
static const char *direction(hm_direction_t d) { return d==HM_DIRECTION_MIC?"mic":d==HM_DIRECTION_SPEAKER?"speaker":"none"; }
static void clear_metadata(hm_core_t *c) { memset(c->metadata_snapshot,0,sizeof c->metadata_snapshot); memset(c->metadata_expiry,0,sizeof c->metadata_expiry); c->metadata_distance_expiry=0; memset(&c->s.tele,0,sizeof c->s.tele); memset(&c->s.rig,0,sizeof c->s.rig); memset(&c->s.position,0,sizeof c->s.position); }
static void clear_talker(hm_core_t *c) { clear_metadata(c); c->s.turn_id=0; c->s.talker_uid=0; c->s.talker_active=false; c->s.talker_callsign[0]=0; }
static bool pop(hm_core_t *c,char out[513]) { if(!c->event_count) { if(out) out[0]=0; return false; } if(out) strcpy(out,c->events[c->event_head]); c->event_head=(c->event_head+1)%4; c->event_count--; return true; }
static void event(hm_core_t *c,const char *name,cJSON *p) {
    if(!c->s.sid) { cJSON_Delete(p); return; }
    cJSON *j=object(); num(j,"v",1); num(j,"id",0); num(j,"sid",c->s.sid); str(j,"event",name); num(j,"n",++c->event_seq); cJSON_AddItemToObject(j,"p",p);
    if(c->event_count==4) { c->event_head=(c->event_head+1)%4; c->event_count--; }
    emit(j,c->events[(c->event_head+c->event_count)%4]); c->event_count++;
}
static void stop_stream(hm_core_t *c,const char *reason,bool notify) {
    if(c->s.stream_id&&notify) { cJSON *p=object(); num(p,"stream_id",c->s.stream_id); str(p,"reason",reason); event(c,"stream.ended",p); }
    if((c->s.direction==HM_DIRECTION_MIC||c->s.pending_ptt)&&c->s.physical_ptt) {
        c->s.wait_release=true;snprintf(c->s.stop_reason,sizeof c->s.stop_reason,"%s",reason);
    }
    c->s.stream_id=0; c->s.direction=HM_DIRECTION_NONE; c->s.next_seq=0; c->s.grant_end=0; c->s.tx_seq=0; c->first_credit=false; c->first_pcm=false; c->s.pending_ptt=false;
}
static void invalidate(hm_core_t *c) { stop_stream(c,"lease_expired",false); c->s.sid=0; c->format=2; c->s.audio_ready=false; c->s.ready=false; c->s.context_id=0; c->s.press_id=0; c->s.wait_release=c->s.physical_ptt; c->s.tx_allowed=false; c->s.muted=true; strcpy(c->s.network,"offline"); strcpy(c->s.phase,"idle"); c->s.callsign[0]=c->s.server[0]=0; clear_talker(c); c->event_count=0; c->turn_high=c->stream_high=0; c->stats_stream=0; memset(&c->stats,0,sizeof c->stats); memset(c->cache,0,sizeof c->cache); c->cache_next=0; }
void hm_core_init(hm_core_t *c,const char *id,uint8_t volume) { memset(c,0,sizeof *c); snprintf(c->device_id,sizeof c->device_id,"%s",id); c->s.volume=volume>100?100:volume; c->s.battery=-1; c->audio_available=true; invalidate(c); }
void hm_core_open(hm_core_t *c,uint16_t max,uint64_t now) { hm_core_close(c); c->s.connected=true; c->gatt_max=max; c->last_flow_ms=now; }
void hm_core_close(hm_core_t *c) { invalidate(c); c->s.connected=false; c->request_high=0; memset(&c->hello,0,sizeof c->hello); }
void hm_core_stop(hm_core_t *c,const char *reason,char out[513]) { stop_stream(c,reason,true); c->s.wait_release=c->s.physical_ptt; c->s.pending_ptt=false; pop(c,out); }
static void expire_metadata(hm_core_t *c,uint64_t now) {
    if(c->s.tele.valid&&now>=c->s.tele.expires_ms) memset(&c->s.tele,0,sizeof c->s.tele);
    if(c->s.rig.valid&&now>=c->s.rig.expires_ms) memset(&c->s.rig,0,sizeof c->s.rig);
    if(c->s.position.valid&&now>=c->s.position.expires_ms) memset(&c->s.position,0,sizeof c->s.position);
    if(c->s.position.distance_known&&now>=c->s.position.distance_expires_ms) { c->s.position.distance_known=false; c->s.position.distance_m=c->s.position.ref_age_s=0; }
}
static void maintain(hm_core_t *c,uint64_t now) {
    expire_metadata(c,now);
    if(c->s.sid&&now-c->last_flow_ms>=3000) invalidate(c);
    if(c->s.pending_ptt&&now-c->press_started_ms>=1000) { c->s.pending_ptt=false; c->s.wait_release=true; c->s.rejected=true; strcpy(c->s.stop_reason,"timeout"); cJSON *p=object(); num(p,"press_id",c->s.press_id); str(p,"reason","timeout"); event(c,"ptt.rejected",p); }
    if(c->s.direction==HM_DIRECTION_MIC) {
        const char *reason=NULL;
        if(now-c->stream_started_ms>=60000) reason="tx_timeout";
        else if(!c->first_credit&&now-c->stream_started_ms>=300) reason="credit_timeout";
        else if(c->first_credit&&now-c->mic_flow_ms>=500) reason="lease_expired";
        if(reason) stop_stream(c,reason,true);
    } else if(c->s.direction==HM_DIRECTION_SPEAKER&&now-(c->first_pcm?c->last_pcm_ms:c->stream_started_ms)>=300) stop_stream(c,"audio_timeout",true);
}
bool hm_core_tick(hm_core_t *c,uint64_t now,char out[513]) { maintain(c,now); return pop(c,out); }
void hm_core_get_snapshot(hm_core_t *c,uint64_t now,hm_snapshot_t *s) { expire_metadata(c,now); *s=c->s; if(s->tele.valid) s->tele.age_s+=(uint32_t)((now-s->tele.received_ms)/1000); if(s->rig.valid) s->rig.age_s+=(uint32_t)((now-s->rig.received_ms)/1000); if(s->position.valid) { s->position.age_s+=(uint32_t)((now-s->position.received_ms)/1000); if(s->position.distance_known) s->position.ref_age_s+=(uint32_t)((now-s->position.distance_received_ms)/1000); } }
static cJSON *status(const hm_core_t *c) {
    cJSON *j=object(),*r=object(); num(j,"v",1); num(j,"sid",c->s.sid); num(j,"context_id",c->s.context_id); num(j,"turn_id",c->s.turn_id); flag(j,"ready",c->s.ready); flag(j,"physical_ptt",c->s.physical_ptt); num(j,"press_id",c->s.press_id); num(j,"stream_id",c->s.stream_id); str(j,"direction",direction(c->s.direction)); num(j,"volume",c->s.volume); flag(j,"muted",c->s.muted); if(c->s.battery<0) cJSON_AddNullToObject(j,"battery"); else num(j,"battery",(uint32_t)c->s.battery);
    str(r,"network",c->s.network); str(r,"phase",c->s.phase); str(r,"callsign",c->s.callsign); str(r,"server",c->s.server); flag(r,"tx_allowed",c->s.tx_allowed); cJSON_AddItemToObject(j,"radio",r); return j;
}
void hm_core_status(const hm_core_t *c,char out[513]) { emit(status(c),out); }
void hm_core_info(const hm_core_t *c,char out[513]) { snprintf(out,513,"{\"product\":\"rf-link-handmic\",\"device_id\":\"%s\",\"fw\":\"1.4.0\",\"major\":[1],\"minor\":1,\"formats\":[2],\"sample_rate\":8000,\"frame_samples\":160,\"audio_bytes\":100,\"credit_window\":32,\"control_bytes\":512,\"pairing\":\"sc_passkey\"}",c->device_id); }
bool hm_core_set_volume(hm_core_t *c,uint8_t v,char out[513]) { if(v>100||(c->save_volume&&!c->save_volume(v,c->save_volume_ctx))) { if(out) out[0]=0; return false; } hm_core_preview_volume(c,v,out); return true; }
void hm_core_set_battery(hm_core_t *c,int v,char out[513]) { c->s.battery=(v<0||v>100)?-1:v; cJSON *p=object(); if(c->s.battery<0) cJSON_AddNullToObject(p,"value"); else num(p,"value",(uint32_t)c->s.battery); event(c,"battery.changed",p); pop(c,out); }
bool hm_core_press(hm_core_t *c,bool down,uint64_t now,char out[513]) {
    maintain(c,now);
    if(out) out[0]=0;
    if(down==c->s.physical_ptt) return false;
    c->s.physical_ptt=down;
    c->s.stop_reason[0]=0;
    if(!down) { if(c->s.direction==HM_DIRECTION_MIC) stop_stream(c,"release",true); c->s.pending_ptt=false; c->s.wait_release=false; c->s.rejected=false; }
    else { if(!c->s.ready||c->s.wait_release||c->s.press_id==UINT32_MAX) { c->s.wait_release=true; if(c->s.press_id==UINT32_MAX) invalidate(c); return false; } c->s.press_id++; c->press_started_ms=now; c->s.pending_ptt=!strcmp(c->s.network,"online")&&c->s.tx_allowed; c->s.rejected=!c->s.pending_ptt; c->s.wait_release=c->s.rejected; if(c->s.rejected) strcpy(c->s.stop_reason,!strcmp(c->s.phase,"busy")?"busy":"not_ready"); }
    if(!c->s.sid) return false;
    cJSON *p=object(); num(p,"press_id",c->s.press_id); flag(p,"down",down); event(c,"ptt",p); return pop(c,out);
}
static uint32_t read32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void write32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8)); }
bool hm_core_on_flow(hm_core_t *c,const uint8_t *b,size_t len,uint64_t now) {
    maintain(c,now);
    if(len!=24||!c->s.sid||b[0]!=1||b[1]||b[2]||b[3]||read32(b+4)!=c->s.sid||read32(b+20)) return false;
    uint32_t id=read32(b+8),n=read32(b+12),end=read32(b+16);
    if(c->s.direction==HM_DIRECTION_MIC) {
        if(id!=c->s.stream_id||end<n||end-n>32||!end||n>UINT32_MAX-32) return false;
        if(c->first_credit&&(n<c->peer_next||end<c->peer_end)) return false;
        if(!c->first_credit) { c->tx_clock_ms=now; c->tx_clock_seq=n; }
        c->peer_next=n; c->peer_end=end; c->first_credit=true; c->mic_flow_ms=now;
    } else if(id||n||end) return false;
    c->last_flow_ms=now; return true;
}
bool hm_core_make_flow(const hm_core_t *c,uint8_t out[24]) {
    if(!c->s.sid) return false;
    memset(out,0,24); out[0]=1; out[1]=c->s.physical_ptt?1:0; write32(out+4,c->s.sid); write32(out+20,c->s.press_id);
    if(c->s.direction==HM_DIRECTION_SPEAKER) { write32(out+8,c->s.stream_id); write32(out+12,c->s.next_seq); write32(out+16,c->s.grant_end); } return true;
}
void hm_core_advance(hm_core_t *c,uint32_t n,uint64_t now) {
    if(c->s.direction!=HM_DIRECTION_SPEAKER||n<=c->s.next_seq) return;
    if(n>UINT32_MAX-32) { stop_stream(c,"sequence_exhausted",true); return; }
    uint32_t delta=n-c->s.next_seq; c->received_mask=delta>=32?0:c->received_mask>>delta; c->s.next_seq=n; c->s.grant_end=n+32; (void)now;
}
bool hm_core_speaker_slot(hm_core_t *c,uint64_t now,uint32_t *seq) {
    maintain(c,now);
    if(c->s.direction!=HM_DIRECTION_SPEAKER||!c->first_pcm||now<c->consume_ms) return false;
    *seq=c->s.next_seq;
    if(!(c->received_mask&1)) { c->stats.underrun++; c->stats.seq_missing++; }
    c->consume_ms+=hm_core_frame_ms(c); hm_core_advance(c,c->s.next_seq+1,now); return true;
}
bool hm_core_audio_accept(hm_core_t *c,const uint8_t *b,size_t len,uint64_t now) {
    maintain(c,now);
    if(len!=hm_core_audio_bytes(c)||b[0]!=1||(b[1]&~1)||b[2]!=c->format||b[3]!=hm_core_frame_samples(c)||!c->s.sid||read32(b+4)!=c->s.sid||c->s.direction!=HM_DIRECTION_SPEAKER||read32(b+8)!=c->s.stream_id) { if(c->stats_stream) c->stats.late++; return false; }
    if(!hm_adpcm_valid(b+16,len-16)) { c->stats.late++; return false; }
    if(!c->first_pcm&&!(b[1]&1)) { c->stats.late++; return false; }
    uint32_t seq=read32(b+12); if(seq<c->s.next_seq) { c->stats.late++; return false; } if(seq>=c->s.grant_end) { c->stats.overflow++; return false; }
    uint32_t bit=UINT32_C(1)<<(seq-c->s.next_seq); if(c->received_mask&bit) { c->stats.duplicate++; return false; }
    if(!c->first_pcm) {
        hm_core_advance(c,seq,now);
        c->consume_ms=now+60;
    } else if((b[1]&1)&&seq>c->s.next_seq) {
        hm_core_advance(c,seq,now);
        /* Drop old audio without restarting prebuffer or catching up stale ticks. */
        if(c->consume_ms<now) c->consume_ms=now;
    }
    c->first_pcm=true; c->last_pcm_ms=now; c->received_mask|=UINT32_C(1)<<(seq-c->s.next_seq); c->rx_seen_high=seq>c->rx_seen_high?seq:c->rx_seen_high; c->stats.rx_packets++;
    unsigned queued=0; for(uint32_t m=c->received_mask;m;m>>=1) queued+=m&1;
    unsigned keep=60/hm_core_frame_ms(c);
    if(queued>120/hm_core_frame_ms(c)&&seq>=keep-1) {
        uint32_t next=seq-(keep-1);
        if(next>c->s.next_seq) {
            c->stats.overflow+=next-c->s.next_seq;
            hm_core_advance(c,next,now);
            if(c->consume_ms<now) c->consume_ms=now;
        }
    }
    return true;
}
static bool mic_header(hm_core_t *c,uint64_t now,uint8_t out[HM_AUDIO_BYTES]) {
    maintain(c,now);
    if(c->s.direction!=HM_DIRECTION_MIC||!c->first_credit||!c->s.physical_ptt||c->s.wait_release) return false;
    uint64_t clock_seq=(uint64_t)c->tx_clock_seq+(now-c->tx_clock_ms)/hm_core_frame_ms(c);
    if(clock_seq>=UINT32_MAX-32) { stop_stream(c,"sequence_exhausted",true); return false; }
    if(clock_seq>c->s.tx_seq) { c->stats.overflow+=(uint32_t)clock_seq-c->s.tx_seq; c->s.tx_seq=(uint32_t)clock_seq; c->tx_discontinuity=true; }
    if(c->s.tx_seq>=UINT32_MAX-32) { stop_stream(c,"sequence_exhausted",true); return false; }
    if(c->s.tx_seq<c->peer_next) { c->s.tx_seq=c->peer_next; c->tx_discontinuity=true; }
    uint32_t seq=c->s.tx_seq++;
    if(seq>=c->peer_end) { c->tx_discontinuity=true; c->stats.overflow++; return false; }
    memset(out,0,16); out[0]=1; out[1]=c->tx_discontinuity?1:0; out[2]=c->format; out[3]=(uint8_t)hm_core_frame_samples(c); write32(out+4,c->s.sid); write32(out+8,c->s.stream_id); write32(out+12,seq); c->tx_discontinuity=false; c->stats.tx_packets++; return true;
}
bool hm_core_mic_frame(hm_core_t *c,const uint8_t *pcm,uint64_t now,uint8_t out[HM_AUDIO_BYTES]) {
    if(!mic_header(c,now,out)) return false;
    int16_t samples[160];memcpy(samples,pcm,sizeof samples);hm_adpcm_encode(samples,out+16);
    return true;
}
static bool oneof(const char *s,const char *a,const char *b,const char *d,const char *e) { return !strcmp(s,a)||!strcmp(s,b)||!strcmp(s,d)||!strcmp(s,e); }
static bool reason_ok(const cJSON *p) { char r[33]; return text(p,"reason",r,32); }
static bool nullable_u32(const cJSON *p,const char *key,uint32_t *n,bool *known) { cJSON *j=get(p,key); if(!j) return false; *known=!cJSON_IsNull(j); if(!*known) { *n=0; return true; } return u32(j,n); }
static bool nullable_text(const cJSON *p,const char *key,char *s,size_t max,bool *known) { cJSON *j=get(p,key); if(!j) return false; *known=!cJSON_IsNull(j); if(!*known) { s[0]=0; return true; } return text(p,key,s,max); }
static bool ttl(const cJSON *p,uint32_t *age,uint32_t *valid,bool *known) { return nullable_u32(p,"age_s",age,known)&&number(p,"valid_for_ms",valid)&&*valid<=300000&&(!*known?*valid==0:(*age<=300&&*valid<=(300-*age)*1000)); }
static bool grid_ok(const char *s) { return strlen(s)==6&&s[0]>='A'&&s[0]<='R'&&s[1]>='A'&&s[1]<='R'&&s[2]>='0'&&s[2]<='9'&&s[3]>='0'&&s[3]<='9'&&s[4]>='A'&&s[4]<='X'&&s[5]>='A'&&s[5]<='X'; }
static const char *metadata(hm_core_t *c,const char *op,const cJSON *p,uint64_t now) {
    uint32_t context,turn,uid; char call[25];
    if(!number(p,"context_id",&context)||!context||!number(p,"turn_id",&turn)||!turn||!number(p,"uid",&uid)||!uid||!text(p,"callsign",call,24)||!callsign(call,false)) return "bad_message";
    if(context!=c->s.context_id) return "stale_context";
    bool match=c->s.talker_active&&turn==c->s.turn_id&&uid==c->s.talker_uid&&!strcmp(call,c->s.talker_callsign);
    if(!strcmp(op,"talker.update")) {
        bool active; if(!boolean(p,"active",&active)) return "bad_message";
        if(!active) { if(!match) return "stale_talker"; if(c->s.direction==HM_DIRECTION_SPEAKER) stop_stream(c,"remote_end",true); clear_talker(c); return NULL; }
        if(match) return NULL;
        if(turn<=c->turn_high) return "stale_talker";
        if(c->s.direction==HM_DIRECTION_SPEAKER) stop_stream(c,"talker_changed",true);
        clear_talker(c); c->turn_high=turn; c->s.turn_id=turn; c->s.talker_uid=uid; strcpy(c->s.talker_callsign,call); c->s.talker_active=true; return NULL;
    }
    if(!match) return "stale_talker";
    uint32_t age,valid; bool age_known; if(!ttl(p,&age,&valid,&age_known)) return "bad_message";
    if(!strcmp(op,"station.tele")) {
        hm_tele_t t={0}; bool a,b;
        if(!nullable_u32(p,"frequency_a_hz",&t.frequency_a_hz,&a)||!nullable_u32(p,"frequency_b_hz",&t.frequency_b_hz,&b)||a!=b||a!=age_known||(a&&(!t.frequency_a_hz||!t.frequency_b_hz))) return "bad_message";
        t.valid=a&&valid>0; t.age_s=age; t.received_ms=now; t.expires_ms=now+valid;
        if(c->s.tele.valid&&t.frequency_a_hz==c->s.tele.frequency_a_hz&&t.frequency_b_hz==c->s.tele.frequency_b_hz&&t.age_s==c->s.tele.age_s) { if(t.expires_ms>c->s.tele.expires_ms) t.expires_ms=c->s.tele.expires_ms; t.received_ms=c->s.tele.received_ms; }
        c->s.tele=t; return NULL;
    }
    if(!strcmp(op,"station.rig")) {
        hm_rig_t t={0}; bool a,b,d,e,f;
        if(!nullable_text(p,"grid",t.grid,6,&a)||!nullable_text(p,"model",t.model,15,&b)||!nullable_u32(p,"frequency_hz",&t.frequency_hz,&d)||!nullable_text(p,"antenna",t.antenna,15,&e)||!nullable_u32(p,"height_m",&t.height_m,&f)||a!=b||a!=d||a!=e||a!=f||a!=age_known||(a&&(!grid_ok(t.grid)||!t.model[0]||!t.antenna[0]||!t.frequency_hz))) return "bad_message";
        t.valid=a&&valid>0; t.age_s=age; t.received_ms=now; t.expires_ms=now+valid;
        hm_rig_t *old=&c->s.rig;
        if(old->valid&&t.age_s==old->age_s&&t.frequency_hz==old->frequency_hz&&t.height_m==old->height_m&&!strcmp(t.grid,old->grid)&&!strcmp(t.model,old->model)&&!strcmp(t.antenna,old->antenna)) { if(t.expires_ms>old->expires_ms) t.expires_ms=old->expires_ms; t.received_ms=old->received_ms; }
        c->s.rig=t; return NULL;
    }
    if(!strcmp(op,"station.position")) {
        hm_position_t t={0}; bool ref_known;
        if(!text(p,"source",t.source,8)||!oneof(t.source,"aprs","rig_grid","unknown","unknown")||!boolean(p,"approx",&t.approx)||!nullable_u32(p,"accuracy_m",&t.accuracy_m,&t.accuracy_known)||!nullable_u32(p,"distance_m",&t.distance_m,&t.distance_known)||!nullable_u32(p,"ref_age_s",&t.ref_age_s,&ref_known)||ref_known!=t.distance_known) return "bad_message";
        cJSON *lat=get(p,"lat_e7"),*lon=get(p,"lon_e7");
        if(!strcmp(t.source,"unknown")) { if(!cJSON_IsNull(lat)||!cJSON_IsNull(lon)||t.accuracy_known||age_known||valid||t.distance_known||!t.approx) return "bad_message"; c->s.position=t; return NULL; }
        if(!cJSON_IsNumber(lat)||!cJSON_IsNumber(lon)||lat->valuedouble< -900000000||lat->valuedouble>900000000||lon->valuedouble< -1800000000||lon->valuedouble>1800000000||!age_known||(t.distance_known&&t.ref_age_s>60)||((!strcmp(t.source,"rig_grid")||!t.accuracy_known||t.accuracy_m>100)&&!t.approx)) return "bad_message";
        t.lat_e7=(int32_t)lat->valuedouble; t.lon_e7=(int32_t)lon->valuedouble; t.valid=valid>0; t.age_s=age; t.received_ms=now; t.expires_ms=now+valid;
        uint32_t distance_valid=t.distance_known?(60-t.ref_age_s)*1000:0; if(distance_valid>valid) distance_valid=valid; t.distance_expires_ms=now+distance_valid; t.distance_received_ms=now;
        hm_position_t *old=&c->s.position;
        bool same=old->valid&&t.age_s==old->age_s&&t.lat_e7==old->lat_e7&&t.lon_e7==old->lon_e7&&!strcmp(t.source,old->source)&&t.accuracy_known==old->accuracy_known&&t.accuracy_m==old->accuracy_m;
        if(same) { if(t.expires_ms>old->expires_ms) t.expires_ms=old->expires_ms; t.received_ms=old->received_ms; if(old->distance_known&&t.distance_known&&t.distance_m==old->distance_m&&t.ref_age_s==old->ref_age_s&&t.distance_expires_ms>old->distance_expires_ms) { t.distance_expires_ms=old->distance_expires_ms; t.distance_received_ms=old->distance_received_ms; } }
        if(!t.valid) t.distance_known=false;
        c->s.position=t; expire_metadata(c,now); return NULL;
    }
    return "bad_message";
}
static const char *execute(hm_core_t *c,const char *op,const cJSON *p,uint64_t now,cJSON **result) {
    cJSON *r=*result;
    if(!strcmp(op,"audio.stats")) {
        char section[9]="rx",json[400];
        if(get(p,"section")&&(!text(p,"section",section,8)||(strcmp(section,"rx")&&strcmp(section,"playback"))))return "bad_message";
        if(!c->audio_report)return "unsupported_operation";
        if(!c->audio_report(section,json))return "audio_error";
        cJSON *report=cJSON_Parse(json);
        if(!cJSON_IsObject(report)){cJSON_Delete(report);return "audio_error";}
        cJSON_Delete(r);*result=report;return NULL;
    }
    if(!strcmp(op,"radio.update")) {
        hm_snapshot_t t=c->s; uint32_t context;
        if(!number(p,"context_id",&context)||!context||!text(p,"network",t.network,10)||!oneof(t.network,"offline","connecting","online","online")||!text(p,"phase",t.phase,4)||!oneof(t.phase,"idle","rx","tx","busy")||!text(p,"callsign",t.callsign,24)||!callsign(t.callsign,true)||!text(p,"server",t.server,48)||!boolean(p,"muted",&t.muted)||!boolean(p,"tx_allowed",&t.tx_allowed)) return "bad_message";
        if((!c->s.context_id&&context!=1)||context<c->s.context_id) return "stale_context";
        if(context!=c->s.context_id) { if(c->s.context_id) { stop_stream(c,"context_changed",true); c->s.wait_release=c->s.physical_ptt; } clear_talker(c); c->s.context_id=context; }
        strcpy(c->s.network,t.network); strcpy(c->s.phase,t.phase); strcpy(c->s.callsign,t.callsign); strcpy(c->s.server,t.server); c->s.tx_allowed=t.tx_allowed&&!strcmp(t.network,"online"); c->s.muted=t.muted; c->s.ready=c->s.audio_ready&&c->audio_available;
        if((strcmp(t.network,"online")&&c->s.direction!=HM_DIRECTION_NONE)||(t.muted&&c->s.direction==HM_DIRECTION_SPEAKER)||(!c->s.tx_allowed&&c->s.direction==HM_DIRECTION_MIC)) stop_stream(c,t.muted?"muted":"network_lost",true);
        if(!c->s.tx_allowed&&c->s.pending_ptt) { c->s.pending_ptt=false; c->s.wait_release=c->s.physical_ptt; c->s.rejected=true; strcpy(c->s.stop_reason,!strcmp(c->s.phase,"busy")?"busy":"not_ready"); }
        return NULL;
    }
    if(!strcmp(op,"status.get")) { cJSON_Delete(r); *result=status(c); return NULL; }
    if(!strcmp(op,"stats.get")) {
        num(r,"sid",c->s.sid); num(r,"stream_id",c->stats_stream); num(r,"rx_packets",c->stats.rx_packets); num(r,"tx_packets",c->stats.tx_packets); num(r,"seq_missing",c->stats.seq_missing); num(r,"duplicate",c->stats.duplicate); num(r,"late",c->stats.late); num(r,"overflow",c->stats.overflow); num(r,"underrun",c->stats.underrun); num(r,"io_errors",c->stats.io_errors); num(r,"transport_boot_submitted",c->transport_boot_submitted); num(r,"transport_boot_dropped",c->transport_boot_dropped); return NULL;
    }
    if(!strcmp(op,"volume.set")) { if(c->remote_volume_busy) return "busy"; uint32_t v; if(!number(p,"value",&v)||v>100) return "bad_message"; if(c->save_volume&&!c->save_volume((uint8_t)v,c->save_volume_ctx)) return "storage_error"; c->s.volume=(uint8_t)v; num(r,"value",v); return NULL; }
    if(!strcmp(op,"stop")) { if(!reason_ok(p)) return "bad_message"; stop_stream(c,"user_stop",false); num(r,"stream_id",0); flag(r,"active",false); return NULL; }
    if(!strcmp(op,"ptt.reject")) { uint32_t press; char reason[33]; if(!number(p,"press_id",&press)||!press||!text(p,"reason",reason,32)) return "bad_message"; if(press==c->s.press_id&&c->s.physical_ptt) { if(c->s.direction==HM_DIRECTION_MIC) stop_stream(c,"rejected",true); snprintf(c->s.stop_reason,sizeof c->s.stop_reason,"%s",reason); c->s.rejected=true; c->s.wait_release=true; c->s.pending_ptt=false; } num(r,"press_id",press); flag(r,"rejected",true); return NULL; }
    if(!strcmp(op,"stream.stop")) { uint32_t id; if(!number(p,"stream_id",&id)||!id||!reason_ok(p)) return "bad_message"; if(id==c->s.stream_id) stop_stream(c,"remote_end",false); num(r,"stream_id",id); flag(r,"active",false); return NULL; }
    if(!strcmp(op,"stream.start")) {
        uint32_t id,press,turn; char dir[8];
        if(!number(p,"stream_id",&id)||!id||!number(p,"press_id",&press)||!number(p,"turn_id",&turn)||!text(p,"direction",dir,7)||(!strcmp(dir,"mic")?turn!=0:strcmp(dir,"speaker")||press!=0)) return "bad_message";
        hm_direction_t d=!strcmp(dir,"mic")?HM_DIRECTION_MIC:HM_DIRECTION_SPEAKER;
        if(!c->audio_available) return "audio_error";
        if(id==c->s.stream_id&&(d!=c->s.direction||(d==HM_DIRECTION_MIC&&press!=c->s.press_id)||(d==HM_DIRECTION_SPEAKER&&turn!=c->s.turn_id))) return "stream_conflict";
        if(!c->s.ready) return "not_ready";
        if(d==HM_DIRECTION_MIC&&(!c->s.physical_ptt||c->s.wait_release||c->s.rejected||press!=c->s.press_id||!press)) return "ptt_released";
        if(d==HM_DIRECTION_MIC&&(!c->s.tx_allowed||strcmp(c->s.network,"online"))) return "not_ready";
        if(d==HM_DIRECTION_SPEAKER&&(!c->s.talker_active||turn!=c->s.turn_id)) return "stale_talker";
        if(d==HM_DIRECTION_SPEAKER&&(c->s.muted||strcmp(c->s.network,"online"))) return "not_ready";
        if(id==c->s.stream_id) { if(d!=c->s.direction) return "stream_conflict"; num(r,"stream_id",id); str(r,"direction",dir); flag(r,"active",true); return NULL; }
        if(id<=c->stream_high) return "stream_conflict";
        if(c->s.stream_id) return "busy";
        c->stream_high=id; c->s.stream_id=id; c->stats_stream=id; memset(&c->stats,0,sizeof c->stats); c->s.direction=d; c->stream_started_ms=now; c->s.next_seq=c->s.tx_seq=0; c->s.grant_end=d==HM_DIRECTION_SPEAKER?32:0; c->first_credit=c->first_pcm=false; c->received_mask=c->rx_seen_high=c->peer_next=c->peer_end=0; c->s.pending_ptt=false; c->tx_discontinuity=true;
        num(r,"stream_id",id); str(r,"direction",dir); flag(r,"active",true); return NULL;
    }
    if(!strcmp(op,"talker.update")||!strcmp(op,"station.tele")||!strcmp(op,"station.rig")||!strcmp(op,"station.position")) { if(!c->s.ready) return "not_ready";
        const char *err=metadata(c,op,p,now); if(err||!strcmp(op,"talker.update")) return err;
        unsigned i=!strcmp(op,"station.tele")?0:!strcmp(op,"station.rig")?1:2;
        const char *allowed=i==0?"|context_id|turn_id|uid|callsign|frequency_a_hz|frequency_b_hz|age_s|valid_for_ms|":i==1?"|context_id|turn_id|uid|callsign|grid|model|frequency_hz|antenna|height_m|age_s|valid_for_ms|":"|context_id|turn_id|uid|callsign|source|lat_e7|lon_e7|accuracy_m|age_s|valid_for_ms|distance_m|ref_age_s|approx|";
        cJSON *clean=cJSON_Duplicate(p,true); if(!clean) return "bad_message";
        for(cJSON *field=clean->child,*next;field;field=next) { next=field->next; char key[64]; snprintf(key,sizeof key,"|%s|",field->string); if(strlen(field->string)>40||!strstr(allowed,key)) cJSON_Delete(cJSON_DetachItemViaPointer(clean,field)); }
        cJSON *prior=cJSON_Parse(c->metadata_snapshot[i]); bool same=prior&&cJSON_Compare(prior,clean,true); cJSON_Delete(prior);
        char compact[513]; bool printed=cJSON_PrintPreallocated(clean,compact,sizeof compact,false); cJSON_Delete(clean); if(!printed) return "bad_message";
        uint64_t *expiry=i==0?&c->s.tele.expires_ms:i==1?&c->s.rig.expires_ms:&c->s.position.expires_ms;
        if(same) {
            if(*expiry>c->metadata_expiry[i]) *expiry=c->metadata_expiry[i];
            if(i==2&&c->s.position.distance_expires_ms>c->metadata_distance_expiry) c->s.position.distance_expires_ms=c->metadata_distance_expiry;
        } else { strcpy(c->metadata_snapshot[i],compact); c->metadata_expiry[i]=*expiry; if(i==2) c->metadata_distance_expiry=c->s.position.distance_expires_ms; }
        expire_metadata(c,now); return NULL; }
    return "bad_message";
}
static bool response(hm_core_t *c,uint32_t id,const char *error,cJSON *r,char out[513]) { cJSON *j=object(); num(j,"v",1); num(j,"id",id); num(j,"sid",c->s.sid); flag(j,"ok",!error); if(error) { str(j,"error",error); cJSON_Delete(r); } else cJSON_AddItemToObject(j,"r",r); emit(j,out); return true; }
bool hm_core_command(hm_core_t *c,const char *json,size_t len,uint64_t now,char out[513]) {
    maintain(c,now);
    uint32_t id=0,sid=0,v=0; char op[32]={0},buf[513]; cJSON *j=NULL; const char *error=NULL,*end=NULL;
    if(!len||len>512||memchr(json,0,len)) return response(c,0,"bad_message",NULL,out);
    memcpy(buf,json,len); buf[len]=0;
    /* cJSON uses NUL-terminated strings: reject escaped NUL before decoding. */
    for(size_t i=0;i+5<len;i++) if(buf[i]=='\\') { if(buf[i+1]=='u'&&!memcmp(buf+i+2,"0000",4)) return response(c,0,"bad_message",NULL,out); i++; }
    j=cJSON_ParseWithLengthOpts(buf,len+1,&end,false);
    if(!j||!cJSON_IsObject(j)||end!=buf+len||!tree(j,0,NULL)||!number(j,"v",&v)||v!=1||!number(j,"id",&id)||!id||!number(j,"sid",&sid)||!text(j,"op",op,31)||!cJSON_IsObject(get(j,"p"))) { cJSON_Delete(j); return response(c,id,"bad_message",NULL,out); }
    bool hello=!strcmp(op,"hello");
    if(!c->s.connected) error="not_paired";
    else if(hello&&sid) error="stale_session";
    else if(!hello&&(!c->s.sid||sid!=c->s.sid)) error="stale_session";
    if(error) { cJSON_Delete(j); return response(c,id,error,NULL,out); }
    hm_cache_entry_t *entry=NULL;
    if(hello&&c->hello.id==id) entry=&c->hello;
    if(!entry) for(unsigned i=0;i<16;i++) if(c->cache[i].id==id) { entry=&c->cache[i]; break; }
    if(entry) { if(strcmp(buf,entry->request)) response(c,id,"id_conflict",NULL,out); else strcpy(out,entry->response); cJSON_Delete(j); return true; }
    if(id<=c->request_high) { cJSON_Delete(j); return response(c,id,"stale_request",NULL,out); }
    c->request_high=id;
    cJSON *r=object(),*p=get(j,"p");
    if(hello) {
        invalidate(c);
        uint32_t major,minor,format,write_max;
        if(!number(p,"major",&major)||!number(p,"minor",&minor)||!number(p,"format",&format)||!number(p,"write_max",&write_max)) error="bad_message";
        else if(major!=1) error="unsupported_version";
        else if(format!=2||minor<1) error="unsupported_format";
        else if(!c->audio_available) error="audio_error";
        else {
            uint32_t m=write_max<c->gatt_max?write_max:c->gatt_max; if(m>244) m=244;
            if(m<100) error="mtu_too_small";
            else if(c->sid_counter==UINT32_MAX) error="not_ready";
            else { c->format=(uint8_t)format; c->s.sid=++c->sid_counter; c->s.audio_ready=true; c->value_max=(uint16_t)m; c->event_seq=0; c->last_flow_ms=now;
                num(r,"major",1); num(r,"minor",1); num(r,"format",format); num(r,"rate",8000); num(r,"samples",hm_core_frame_samples(c)); num(r,"frame_ms",hm_core_frame_ms(c)); num(r,"audio_bytes",(uint32_t)hm_core_audio_bytes(c)); num(r,"value_max",m); num(r,"credit_window",32); flag(r,"audio_ready",true); }
        }
    } else { expire_metadata(c,now); error=execute(c,op,p,now,&r); }
    response(c,id,error,r,out);
    entry=hello?&c->hello:&c->cache[c->cache_next++%16]; entry->id=id; strcpy(entry->request,buf); strcpy(entry->response,out); cJSON_Delete(j); return true;
}
void hm_core_set_audio_available(hm_core_t *c,bool available) { c->audio_available=available; if(!available) { c->stats.io_errors++; stop_stream(c,"audio_error",true); c->s.audio_ready=false; c->s.ready=false; } }

void hm_core_set_volume_store(hm_core_t *c,bool (*save)(uint8_t,void *),void *ctx) { c->save_volume=save; c->save_volume_ctx=ctx; }

void hm_core_preview_volume(hm_core_t *c,uint8_t v,char out[513]) { c->s.volume=v; cJSON *p=object();num(p,"value",v);event(c,"volume.changed",p);pop(c,out); }
