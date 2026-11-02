#include "lab_model.h"
#include <string.h>
bool lab_offer(lab_model_t *s, const uint8_t key[32]) {
    if(s->bound) return memcmp(s->key,key,32)==0;
    if(s->candidate) return memcmp(s->proposed,key,32)==0;
    memcpy(s->proposed,key,32); s->candidate=true; return true;
}
bool lab_confirm(lab_model_t *s) {
    if(s->bound || !s->candidate) return false;
    memcpy(s->key,s->proposed,32); s->bound=true; s->candidate=false; return true;
}
bool lab_request(lab_model_t *s,const char *message,uint64_t now) {
    if(!s->bound || s->pending || strlen(message)>=sizeof(s->message)) return false;
    if(++s->id==0) ++s->id;
    strcpy(s->message,message); s->started_ms=now; s->pending=true; return true;
}
bool lab_result(lab_model_t *s,uint32_t id,bool valid,uint64_t now) {
    lab_tick(s,now);
    if(!s->pending || id!=s->id || !valid) { s->rejected++; return false; }
    s->elapsed_ms=now-s->started_ms; s->pending=false; s->passed++; return true;
}
bool lab_tick(lab_model_t *s,uint64_t now) {
    if(s->pending && now-s->started_ms>=LAB_TIMEOUT_MS) {
        s->pending=false; s->timed_out++; return true;
    }
    return false;
}
void lab_disconnect(lab_model_t *s) { s->pending=false; s->candidate=false; memset(s->proposed,0,32); }
void lab_clear(lab_model_t *s) {
    uint32_t id=s->id; memset(s,0,sizeof(*s)); s->id=id;
}
static int digit(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
bool lab_unhex(const char *text,uint8_t *out,size_t bytes) {
    if(!text || strlen(text)!=bytes*2)return false;
    for(size_t i=0;i<bytes;i++) {
        int a=digit(text[i*2]),b=digit(text[i*2+1]);
        if(a<0||b<0)return false;
        out[i]=(uint8_t)((a<<4)|b);
    }
    return true;
}
void lab_hex(const uint8_t *in,size_t bytes,char *out) {
    static const char hex[]="0123456789abcdef";
    for(size_t i=0;i<bytes;i++){out[i*2]=hex[in[i]>>4];out[i*2+1]=hex[in[i]&15];}
    out[bytes*2]=0;
}
