#include "lab_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    lab_model_t s={0}; uint8_t a[32]={1},b[32]={2},out[32]; char hex[65];
    assert(!lab_request(&s,"hello",0)); assert(!lab_confirm(&s));
    assert(lab_offer(&s,a)); assert(!lab_offer(&s,b)); assert(lab_confirm(&s));
    assert(lab_offer(&s,a)); assert(!lab_offer(&s,b)); assert(!lab_confirm(&s));
    assert(lab_request(&s,"hello",100)); uint32_t id=s.id;
    assert(!lab_request(&s,"replace",101)); assert(!lab_result(&s,id+1,true,102));
    assert(!lab_result(&s,id,false,103)); assert(s.pending);
    assert(lab_result(&s,id,true,150)); assert(s.elapsed_ms==50 && s.passed==1);
    assert(!lab_result(&s,id,true,151));
    assert(lab_request(&s,"next",200)); assert(!lab_result(&s,id,true,201));
    assert(!lab_tick(&s,10199)); assert(lab_tick(&s,10200)); assert(s.timed_out==1);
    assert(!lab_result(&s,s.id,true,10201));
    assert(lab_request(&s,"again",11000)); lab_disconnect(&s); assert(s.bound&&!s.pending);
    id=s.id; lab_clear(&s); assert(!s.bound&&!s.candidate&&s.id==id);
    assert(lab_offer(&s,b)); lab_disconnect(&s); assert(!s.candidate);
    lab_hex(a,32,hex); assert(lab_unhex(hex,out,32)&&memcmp(a,out,32)==0);
    assert(!lab_unhex("00",out,32)); hex[1]='z'; assert(!lab_unhex(hex,out,32));
    puts("BLE lab model: PASS (binding, replacement, replay, timeout, disconnect, hex)");
}
