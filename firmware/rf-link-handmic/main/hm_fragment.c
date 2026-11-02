#include "hm_fragment.h"
#include <string.h>
static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1]<<8; }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
void hm_fragment_reset(hm_fragment_t *rx) { memset(rx, 0, sizeof(*rx)); }
static void discard(hm_fragment_t *r) {
    if (r->active && !r->complete) r->dropped++;
    r->active=false;r->complete=false;r->used=0;
}
bool hm_fragment_expire(hm_fragment_t *rx, uint64_t now_ms) {
    if (rx->active && !rx->complete && now_ms-rx->started_ms>=2000) { discard(rx);return true; }
    return false;
}
hm_fragment_result_t hm_fragment_push(hm_fragment_t *r, const uint8_t *v, size_t n, uint64_t now) {
    bool expired=hm_fragment_expire(r,now);
    if (!v || n<=8 || n>520) { discard(r);return HM_FRAGMENT_BAD; }
    uint32_t id=u32(v);uint16_t off=u16(v+4),total=u16(v+6);size_t count=n-8;
    if (!total || total>512 || off>=total || count>(size_t)(total-off)) { discard(r);return HM_FRAGMENT_BAD; }
    if (r->active && r->id==id) {
        if (total!=r->total) { discard(r);return HM_FRAGMENT_BAD; }
        if (off<r->used) {
            if (r->fragment_lengths[off]==count && off+count<=r->used && !memcmp(r->bytes+off,v+8,count)) return HM_FRAGMENT_DUPLICATE;
            discard(r);return HM_FRAGMENT_BAD;
        }
    } else {
        if (off) { discard(r);return HM_FRAGMENT_BAD; }
        discard(r);memset(r->fragment_lengths,0,sizeof(r->fragment_lengths));
        r->id=id;r->total=total;r->started_ms=now;r->active=true;
    }
    if (expired && off) return HM_FRAGMENT_BAD;
    if (off!=r->used || r->complete) { discard(r);return HM_FRAGMENT_BAD; }
    memcpy(r->bytes+off,v+8,count);r->fragment_lengths[off]=(uint16_t)count;r->used+=(uint16_t)count;
    if (r->used==r->total) { r->complete=true;return HM_FRAGMENT_COMPLETE; }
    return HM_FRAGMENT_MORE;
}
size_t hm_fragment_encode(uint8_t *v,size_t cap,uint32_t id,uint16_t off,const uint8_t *json,size_t total,uint16_t limit) {
    if (!v || !json || !total || total>512 || off>=total || limit<9 || limit>244 || cap<9) return 0;
    size_t count=total-off;
    size_t available=cap<limit?cap:limit;
    if (count>available-8) count=available-8;
    v[0]=id;v[1]=id>>8;v[2]=id>>16;v[3]=id>>24;
    v[4]=off;v[5]=off>>8;v[6]=(uint16_t)total;v[7]=(uint16_t)total>>8;
    memcpy(v+8,json+off,count);return count+8;
}
