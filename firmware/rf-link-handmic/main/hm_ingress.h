#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
/* Caller serializes these bounded copies with a short critical section, never
 * the core mutex. Flow is absolute credit and the newest update supersedes it. */
#define HM_INGRESS_CAPACITY 6
#define HM_INGRESS_MAX_AGE_MS 120
 typedef struct { uint32_t generation; uint64_t at; uint8_t data[100]; } hm_ingress_item_t;
typedef struct {
    hm_ingress_item_t audio[HM_INGRESS_CAPACITY], flow;
    unsigned head, count; bool flow_pending;
    uint32_t audio_full, flow_replaced, stale, rejected, deferred;
} hm_ingress_t;
static inline bool hm_ingress_push(hm_ingress_t *q,bool flow,uint32_t gen,const uint8_t *data,uint64_t at){
    hm_ingress_item_t *p;
    if(flow){q->flow_replaced+=q->flow_pending;q->flow_pending=true;p=&q->flow;}
    else {if(q->count==HM_INGRESS_CAPACITY){q->audio_full++;return false;}p=&q->audio[(q->head+q->count++)%HM_INGRESS_CAPACITY];}
    p->generation=gen;p->at=at;memcpy(p->data,data,flow?24:100);return true;
}
static inline bool hm_ingress_pending(const hm_ingress_t *q){return q->flow_pending||q->count;}
static inline bool hm_ingress_pop(hm_ingress_t *q,hm_ingress_item_t *out,bool *flow){
    if(q->flow_pending){*out=q->flow;q->flow_pending=false;*flow=true;return true;}
    if(!q->count)return false;
    *out=q->audio[q->head];q->head=(q->head+1)%HM_INGRESS_CAPACITY;q->count--;*flow=false;return true;
}
static inline bool hm_ingress_current(const hm_ingress_item_t *p,uint64_t now,uint32_t gen,bool auth,uint32_t sid,uint32_t stream,bool flow){
    uint32_t psid=(uint32_t)p->data[4]|(uint32_t)p->data[5]<<8|(uint32_t)p->data[6]<<16|(uint32_t)p->data[7]<<24;
    uint32_t pstream=(uint32_t)p->data[8]|(uint32_t)p->data[9]<<8|(uint32_t)p->data[10]<<16|(uint32_t)p->data[11]<<24;
    return auth&&gen==p->generation&&sid&&psid==sid&&pstream==(flow&&pstream==0?0:stream)&&now>=p->at&&now-p->at<=HM_INGRESS_MAX_AGE_MS;
}
