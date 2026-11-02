#include "hm_fragment.h"
#include "hm_tx_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef HM_CCCD_GUARD_TEST
#include "hm_cccd_guard.h"
#include "host/ble_att.h"
static const ble_uuid_t *seen_uuid;
static uint8_t seen_flags,seen_key;
static uint16_t *seen_handle;
static hm_att_access_fn *seen_access;
static void *seen_arg;
static int register_result;
uint16_t ble_uuid_u16(const ble_uuid_t *uuid) { return uuid->value; }
int __real_ble_att_svr_register(const ble_uuid_t *uuid,uint8_t flags,uint8_t key,uint16_t *handle,hm_att_access_fn *access,void *arg) {
    seen_uuid=uuid;seen_flags=flags;seen_key=key;seen_handle=handle;seen_access=access;seen_arg=arg;return register_result;
}
static int dummy_access(uint16_t c,uint16_t a,uint8_t o,uint16_t off,struct os_mbuf **m,void *arg) {
    (void)c;(void)a;(void)o;(void)off;(void)m;(void)arg;return 0;
}
static void test_cccd_guard(void) {
    ble_uuid_t cccd={.value=0x2902},info={.value=0x1234};uint16_t handle=9;int marker=0;
    assert(!hm_cccd_guard_verified());
    assert(!__wrap_ble_att_svr_register(&cccd,0xe3,0,&handle,dummy_access,&marker));
    assert(seen_flags==0xff && seen_key==16);
    assert(seen_uuid==&cccd && seen_handle==&handle && seen_access==dummy_access && seen_arg==&marker);
    assert(!__wrap_ble_att_svr_register(&info,3,7,&handle,dummy_access,&marker));
    assert(seen_flags==3 && seen_key==7 && seen_uuid==&info);
    register_result=37;
    assert(__wrap_ble_att_svr_register(&cccd,3,0,&handle,dummy_access,&marker)==37);
    assert(!hm_cccd_guard_verified()); /* failed registrations do not establish a guard */
    register_result=0;
    assert(!__wrap_ble_att_svr_register(&cccd,3,16,&handle,dummy_access,&marker));
    assert(!hm_cccd_guard_verified());
    assert(!__wrap_ble_att_svr_register(&cccd,3,16,&handle,dummy_access,&marker));
    assert(hm_cccd_guard_verified());
}
#endif
static void test_physical_submit_guard(void) {
    hm_tx_guard_t guard={0};uint32_t submitted=99,dropped=99;
    /* A missing physical gate must fail closed. */
    assert(!hm_tx_guard_submit_allowed(&guard,hm_tx_guard_epoch(&guard)));
    hm_tx_guard_stats(&guard,&submitted,&dropped);assert(submitted==0 && dropped==1);
    _Atomic bool pressed=true;
    hm_tx_guard_set(&guard,&pressed);
    /* Frame was queued while pressed, then RELEASE precedes its actual submit. */
    bool queued_while_pressed=atomic_load(&pressed);assert(queued_while_pressed);
    atomic_store(&pressed,false);
    assert(!hm_tx_guard_submit_allowed(&guard,hm_tx_guard_epoch(&guard)));
    hm_tx_guard_stats(&guard,&submitted,&dropped);assert(submitted==0 && dropped==2);
    atomic_store(&pressed,true);
    assert(hm_tx_guard_submit_allowed(&guard,hm_tx_guard_epoch(&guard)));hm_tx_guard_result(&guard,true);
    assert(hm_tx_guard_submit_allowed(&guard,hm_tx_guard_epoch(&guard)));hm_tx_guard_result(&guard,false);
    hm_tx_guard_drop(&guard,12); /* clear/expiry drops are counted by actual frame count */
    hm_tx_guard_stats(&guard,&submitted,&dropped);assert(submitted==1 && dropped==15);
    hm_tx_guard_set(&guard,NULL);assert(!hm_tx_guard_submit_allowed(&guard,hm_tx_guard_epoch(&guard)));
    hm_tx_guard_stats(&guard,&submitted,NULL);assert(submitted==1);
    hm_tx_guard_stats(&guard,NULL,&dropped);assert(dropped==16);
}
static void test_rapid_repress_cannot_revive_queued_audio(void) {
    hm_tx_guard_t guard={0};_Atomic bool permitted=true;
    hm_tx_guard_set(&guard,&permitted);
    uint32_t old_packet_epoch=hm_tx_guard_epoch(&guard);
    /* RELEASE revokes queued audio immediately, before the worker can clear it. */
    atomic_store(&permitted,false);hm_tx_guard_revoke(&guard);
    /* A fresh press/stream can reopen the latch while old PCM remains queued. */
    atomic_store(&permitted,true);
    assert(!hm_tx_guard_submit_allowed(&guard,old_packet_epoch));
    uint32_t submitted=99,dropped=99;hm_tx_guard_stats(&guard,&submitted,&dropped);
    assert(submitted==0 && dropped==1);
    uint32_t new_packet_epoch=hm_tx_guard_epoch(&guard);
    assert(new_packet_epoch!=old_packet_epoch);
    assert(hm_tx_guard_submit_allowed(&guard,new_packet_epoch));
    /* A revoke after an earlier queue/dequeue check is caught at final submit. */
    hm_tx_guard_revoke(&guard);
    assert(!hm_tx_guard_submit_allowed(&guard,new_packet_epoch));
    hm_tx_guard_stats(&guard,&submitted,&dropped);assert(submitted==0 && dropped==2);
    /* Queueing after the pump's old timestamp cannot underflow packet age. */
    assert(!hm_tx_guard_expired(101,100));
    assert(!hm_tx_guard_expired(101,101));
    assert(!hm_tx_guard_expired(101,221));
    assert(hm_tx_guard_expired(101,222));
}
static hm_fragment_result_t feed(hm_fragment_t *r, uint32_t id, uint16_t off, uint16_t total, const char *s, uint64_t now) {
    uint8_t v[520]; size_t n=strlen(s);
    v[0]=id;v[1]=id>>8;v[2]=id>>16;v[3]=id>>24;
    v[4]=off;v[5]=off>>8;v[6]=total;v[7]=total>>8;
    memcpy(v+8,s,n);return hm_fragment_push(r,v,n+8,now);
}
int main(void) {
    test_physical_submit_guard();
    test_rapid_repress_cannot_revive_queued_audio();
#ifdef HM_CCCD_GUARD_TEST
    test_cccd_guard();
#endif
    hm_fragment_t r;hm_fragment_reset(&r);
    assert(feed(&r,7,0,5,"he",100)==HM_FRAGMENT_MORE);
    assert(feed(&r,7,0,5,"he",101)==HM_FRAGMENT_DUPLICATE);
    assert(feed(&r,7,2,5,"llo",102)==HM_FRAGMENT_COMPLETE);
    assert(r.used==5 && !memcmp(r.bytes,"hello",5));
    assert(feed(&r,7,0,5,"he",103)==HM_FRAGMENT_DUPLICATE);
    assert(feed(&r,7,2,5,"llo",104)==HM_FRAGMENT_DUPLICATE);
    /* Conflict discards transfer, so following tail cannot execute old data. */
    assert(feed(&r,7,0,5,"ha",105)==HM_FRAGMENT_BAD);
    assert(feed(&r,7,2,5,"llo",106)==HM_FRAGMENT_BAD);
    assert(feed(&r,8,0,5,"he",200)==HM_FRAGMENT_MORE);
    assert(feed(&r,8,3,5,"lo",201)==HM_FRAGMENT_BAD);
    assert(feed(&r,9,0,5,"he",300)==HM_FRAGMENT_MORE);
    assert(feed(&r,9,2,6,"llo",301)==HM_FRAGMENT_BAD);
    assert(feed(&r,10,0,5,"he",400)==HM_FRAGMENT_MORE);
    assert(feed(&r,11,2,5,"llo",401)==HM_FRAGMENT_BAD);
    assert(feed(&r,12,0,5,"he",500)==HM_FRAGMENT_MORE);
    assert(!hm_fragment_expire(&r,2499));
    assert(hm_fragment_expire(&r,2500));
    assert(feed(&r,12,2,5,"llo",2501)==HM_FRAGMENT_BAD);
    assert(feed(&r,13,0,5,"he",3000)==HM_FRAGMENT_MORE);
    assert(feed(&r,13,0,5,"he",4999)==HM_FRAGMENT_DUPLICATE);
    assert(feed(&r,13,2,5,"llo",5000)==HM_FRAGMENT_BAD);
    assert(feed(&r,14,0,5,"he",6000)==HM_FRAGMENT_MORE);
    uint32_t dropped=r.dropped;
    assert(feed(&r,15,0,2,"{}",6001)==HM_FRAGMENT_COMPLETE);
    assert(r.dropped==dropped+1);
    assert(feed(&r,16,0,0,"x",6002)==HM_FRAGMENT_BAD);
    assert(feed(&r,16,0,513,"x",6002)==HM_FRAGMENT_BAD);
    assert(feed(&r,16,0,2,"abc",6002)==HM_FRAGMENT_BAD);
    assert(feed(&r,16,0,2,"",6002)==HM_FRAGMENT_BAD);
    uint8_t short_header[8]={0};
    assert(hm_fragment_push(&r,short_header,8,6003)==HM_FRAGMENT_BAD);
    /* An equal overlapping byte range is not an identical repeated fragment. */
    assert(feed(&r,17,0,5,"hel",7000)==HM_FRAGMENT_MORE);
    assert(feed(&r,17,1,5,"el",7001)==HM_FRAGMENT_BAD);
    hm_fragment_reset(&r);assert(!r.active && !r.complete && r.used==0);
    char large[513];memset(large,'a',512);large[512]=0;
    assert(feed(&r,0xffffffff,0,512,large,8000)==HM_FRAGMENT_COMPLETE);
    assert(r.used==512);
    uint8_t wire[244];
    assert(hm_fragment_encode(wire,sizeof(wire),0x12345678,0,(const uint8_t *)large,512,20)==20);
    assert(wire[0]==0x78 && wire[1]==0x56 && wire[2]==0x34 && wire[3]==0x12 && wire[4]==0 && wire[6]==0 && wire[7]==2);
    hm_fragment_reset(&r);
    uint16_t offset=0;
    while (offset<512) {
        size_t n=hm_fragment_encode(wire,sizeof(wire),0x12345678,offset,(const uint8_t *)large,512,20);
        assert(n>=9 && n<=20);
        hm_fragment_result_t result=hm_fragment_push(&r,wire,n,9000+offset);
        offset+=(uint16_t)(n-8);
        assert(result==(offset==512?HM_FRAGMENT_COMPLETE:HM_FRAGMENT_MORE));
    }
    assert(!memcmp(r.bytes,large,512));
    assert(hm_fragment_encode(wire,sizeof(wire),3,511,(const uint8_t *)large,512,244)==9);
    assert(hm_fragment_encode(wire,sizeof(wire),3,512,(const uint8_t *)large,512,244)==0);
    assert(hm_fragment_encode(wire,sizeof(wire),3,0,(const uint8_t *)large,513,244)==0);
    assert(hm_fragment_encode(wire,sizeof(wire),3,0,(const uint8_t *)large,512,8)==0);
    assert(hm_fragment_encode(wire,8,3,0,(const uint8_t *)large,512,20)==0);
    puts("handmic transport adversarial cases PASS");return 0;
}
