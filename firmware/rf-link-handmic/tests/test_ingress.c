#include "hm_ingress.h"
#include <assert.h>
#include <stdio.h>
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
int main(void){
    hm_ingress_t q={0};uint8_t a[100]={0};hm_ingress_item_t item;bool flow;
    put32(a+4,23);put32(a+8,17);
    /* Core lock held: enqueue needs no core lock, all six frames survive. */
    for(unsigned i=0;i<6;i++){put32(a+12,i);assert(hm_ingress_push(&q,false,7,a,100+i*20));}
    assert(!hm_ingress_push(&q,false,7,a,220));assert(q.audio_full==1);
    assert(hm_ingress_push(&q,true,7,a,210));put32(a+12,42);
    assert(hm_ingress_push(&q,true,7,a,215));assert(q.flow_replaced==1);
    assert(hm_ingress_pop(&q,&item,&flow)&&flow&&item.at==215&&item.data[12]==42);
    assert(hm_ingress_pop(&q,&item,&flow)&&!flow&&item.at==100);
    assert(hm_ingress_current(&item,220,7,true,23,17,false));
    assert(!hm_ingress_current(&item,221,7,true,23,17,false));
    assert(!hm_ingress_current(&item,200,8,true,23,17,false));
    assert(!hm_ingress_current(&item,200,7,false,23,17,false));
    assert(!hm_ingress_current(&item,200,7,true,24,17,false));
    assert(!hm_ingress_current(&item,200,7,true,23,18,false));
    assert(!hm_ingress_current(&item,200,7,true,0,0,false));
    for(unsigned i=1;i<6;i++){assert(hm_ingress_pop(&q,&item,&flow)&&!flow);assert(item.data[12]==i);}
    assert(!hm_ingress_pending(&q));assert(!hm_ingress_pop(&q,&item,&flow));
    put32(a+8,0);assert(hm_ingress_push(&q,true,7,a,300));
    assert(hm_ingress_pop(&q,&item,&flow)&&flow);assert(hm_ingress_current(&item,300,7,true,23,17,true));
    puts("bounded ingress: capacity, coalescing/priority, stale/auth/generation/session/stream PASS");
}
