#include "hm_adpcm.h"
#include <string.h>

/* IMA/DVI reference step and index adaptation tables. No heap or floating point. */
static const int steps[89]={
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
    50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,
    337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
    1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
    5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
    18500,20350,22385,24623,27086,29794,32767
};
static const int indices[8]={-1,-1,-1,-1,2,4,6,8};
static int expand(unsigned code,int *predictor,int *index) {
    int step=steps[*index], delta=step>>3;
    if(code&4) delta+=step;
    if(code&2) delta+=step>>1;
    if(code&1) delta+=step>>2;
    *predictor+=(code&8)?-delta:delta;
    if(*predictor>32767) *predictor=32767;
    if(*predictor< -32768) *predictor= -32768;
    *index+=indices[code&7];
    if(*index<0) *index=0;
    if(*index>88) *index=88;
    return *predictor;
}
bool hm_adpcm_valid(const uint8_t *b,size_t n) {
    return b&&n==HM_ADPCM_BYTES&&b[2]<=88&&!b[3]&&!(b[83]&0xf0);
}
bool hm_adpcm_decode(const uint8_t *b,size_t n,int16_t pcm[160]) {
    if(!pcm||!hm_adpcm_valid(b,n)) return false;
    int predictor=b[0]|((unsigned)b[1]<<8), index=b[2];
    if(predictor>=32768) predictor-=65536;
    pcm[0]=(int16_t)predictor;
    for(unsigned i=1;i<160;i++) {
        unsigned code=(b[4+(i-1)/2]>>(((i-1)%2)*4))&15;
        pcm[i]=(int16_t)expand(code,&predictor,&index);
    }
    return true;
}
void hm_adpcm_encode(const int16_t pcm[160],uint8_t b[84]) {
    memset(b,0,84);
    int predictor=pcm[0], index=0, delta=(int)pcm[1]-pcm[0];
    if(delta<0) delta=-delta;
    /* Seed each block from its own first delta; no cross-packet state. */
    while(index<88&&steps[index]<delta) index++;
    b[0]=(uint8_t)predictor; b[1]=(uint8_t)((uint16_t)predictor>>8); b[2]=(uint8_t)index;
    for(unsigned i=1;i<160;i++) {
        int diff=(int)pcm[i]-predictor, step=steps[index];
        unsigned code=diff<0?8:0;
        if(diff<0) diff=-diff;
        if(diff>=step) { code|=4; diff-=step; }
        if(diff>=(step>>1)) { code|=2; diff-=step>>1; }
        if(diff>=(step>>2)) code|=1;
        expand(code,&predictor,&index);
        b[4+(i-1)/2]|=(uint8_t)(code<<(((i-1)%2)*4));
    }
}
