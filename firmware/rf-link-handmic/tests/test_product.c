#include "hm_product.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static hm_product_t p;
static hm_snapshot_t s;
static bool volume_ok, theme_ok, spectrum_ok, stored_spectrum;
static unsigned spectrum_writes;
static unsigned volume_writes, theme_writes;
static uint8_t stored_volume;
static char stored_theme[24];
static bool volume_store(uint8_t v, void *ctx) {
    assert(ctx==&p); volume_writes++; if (!volume_ok) return false;
    stored_volume=v; return true;
}
static bool theme_store(const char *id, void *ctx) {
    assert(ctx==&p); theme_writes++; if (!theme_ok) return false;
    snprintf(stored_theme,sizeof stored_theme,"%s",id); return true;
}
static bool spectrum_store(bool enabled, void *ctx) {
    assert(ctx==&p); spectrum_writes++; if(!spectrum_ok) return false;
    stored_spectrum=enabled; return true;
}
static void setup(bool bonded) {
    memset(&s,0,sizeof s); s.connected=true; s.audio_ready=true; s.ready=true;
    s.volume=90; s.sid=1; s.context_id=1;
    spectrum_ok=true; spectrum_writes=0; stored_spectrum=false;
    volume_ok=theme_ok=true; volume_writes=theme_writes=0; stored_volume=90;
    strcpy(stored_theme,"phosphor_green");
    hm_product_init(&p,90,"phosphor_green",(hm_product_store_t){.store_volume=volume_store,.store_theme=theme_store,.ctx=&p,.store_spectrum=spectrum_store});
    hm_product_observe(&p,&s,bonded,bonded,false,0);
}
static hm_product_actions_t click(hm_product_key_t k,uint64_t t) {
    hm_product_actions_t a=hm_product_button(&p,k,HM_KEY_PRESS,t);
    a|=hm_product_button(&p,k,HM_KEY_RELEASE,t+10);
    return a|hm_product_button(&p,k,HM_KEY_CLICK,t+10);
}
static void menu(unsigned item,uint64_t t) {
    assert(p.page==HM_PAGE_MAIN);
    assert(!hm_product_button(&p,HM_KEY_UP,HM_KEY_PRESS,t));
    assert(!hm_product_tick(&p,t+500)); assert(p.page==HM_PAGE_MENU);
    click(HM_KEY_UP,t+501); /* held long is consumed, never adjust volume */
    for(unsigned i=0;i<item;i++) assert(!click(HM_KEY_DOWN,t+520+i*20));
    assert(!click(HM_KEY_OK,t+700));
}
static void long_ok(uint64_t t) {
    assert(!hm_product_button(&p,HM_KEY_OK,HM_KEY_PRESS,t));
    assert(!hm_product_button(&p,HM_KEY_OK,HM_KEY_LONG,t+500));
    assert(p.page!=HM_PAGE_MAIN);
    hm_product_tick(&p,t+800); assert(p.page==HM_PAGE_MAIN);
    assert(!hm_product_button(&p,HM_KEY_OK,HM_KEY_RELEASE,t+801));
    assert(!hm_product_button(&p,HM_KEY_OK,HM_KEY_CLICK,t+801));
}
static void test_navigation_and_ptt(void) {
    setup(true); menu(0,100); assert(p.page==HM_PAGE_HISTORY && p.volume==90);
    long_ok(1000); assert(p.volume==90);
    assert(hm_product_button(&p,HM_KEY_OK,HM_KEY_PRESS,2000)==HM_ACTION_PTT_DOWN);
    s.pending_ptt=true; hm_product_observe(&p,&s,true,true,false,2001);
    hm_product_button(&p,HM_KEY_UP,HM_KEY_PRESS,2010); hm_product_tick(&p,2600);
    assert(p.page==HM_PAGE_MAIN);
    assert(hm_product_button(&p,HM_KEY_OK,HM_KEY_RELEASE,2700)==HM_ACTION_PTT_UP);
    hm_product_button(&p,HM_KEY_OK,HM_KEY_CLICK,2700);
    s.pending_ptt=false; hm_product_observe(&p,&s,true,true,false,2701);
    hm_product_button(&p,HM_KEY_UP,HM_KEY_RELEASE,2800); hm_product_button(&p,HM_KEY_UP,HM_KEY_CLICK,2800);
    setup(true); menu(1,100); assert(p.page==HM_PAGE_DETAIL);
    assert(!click(HM_KEY_DOWN,1000)); assert(p.detail_page==1);
    assert(!click(HM_KEY_OK,1100)); assert(p.page==HM_PAGE_MAIN);
    setup(true); menu(0,100); hm_product_button(&p,HM_KEY_OK,HM_KEY_PRESS,1000);
    hm_product_tick(&p,1800); assert(p.page==HM_PAGE_MAIN);
    assert(!hm_product_button(&p,HM_KEY_OK,HM_KEY_PRESS,1900)); /* still held */
    hm_product_button(&p,HM_KEY_OK,HM_KEY_RELEASE,1910);
    assert(hm_product_button(&p,HM_KEY_OK,HM_KEY_PRESS,1920)==HM_ACTION_PTT_DOWN);
}
static void test_volume_transactions(void) {
    setup(true); assert(!click(HM_KEY_DOWN,100)); assert(p.volume==85 && volume_writes==0);
    assert(hm_product_volume_busy(&p)); hm_product_tick(&p,609); assert(volume_writes==0);
    hm_product_tick(&p,610); assert(stored_volume==85 && !hm_product_volume_busy(&p));
    assert(!click(HM_KEY_DOWN,700)); volume_ok=false; hm_product_tick(&p,1210);
    assert(p.volume==85 && p.notice==HM_NOTICE_VOLUME_FAILED);
    volume_ok=true; menu(2,1300); assert(p.page==HM_PAGE_VOLUME && hm_product_volume_busy(&p));
    click(HM_KEY_UP,2200); assert(p.volume==90 && p.saved_volume==85);
    long_ok(2300); assert(p.volume==85 && stored_volume==85);
    menu(2,3300); click(HM_KEY_DOWN,4200); volume_ok=false; click(HM_KEY_OK,4300);
    assert(p.page==HM_PAGE_VOLUME && p.volume==85 && p.notice==HM_NOTICE_VOLUME_FAILED);
    volume_ok=true; click(HM_KEY_UP,4400); click(HM_KEY_OK,4500);
    assert(p.page==HM_PAGE_MENU && p.volume==90 && stored_volume==90);
    hm_product_remote_volume(&p,35); assert(p.volume==35 && p.saved_volume==35);
    p.page=HM_PAGE_MAIN; menu(2,5000);
    hm_product_remote_volume(&p,5); assert(p.volume==35 && p.saved_volume==35); /* busy leaves edit intact */
    hm_product_button(&p,HM_KEY_DOWN,HM_KEY_PRESS,6000); hm_product_tick(&p,6500);
    hm_product_tick(&p,6650); assert(p.volume==25);
    hm_product_button(&p,HM_KEY_DOWN,HM_KEY_RELEASE,6660); hm_product_button(&p,HM_KEY_DOWN,HM_KEY_CLICK,6660);
    assert(p.volume==25); long_ok(7000); assert(p.volume==35);
    setup(true); click(HM_KEY_DOWN,100); menu(2,200); assert(p.volume==85 && p.saved_volume==85 && stored_volume==85);
}
static void test_theme_transactions(void) {
    setup(true); menu(4,100); assert(p.page==HM_PAGE_THEME);
    click(HM_KEY_DOWN,1000); assert(p.theme==HM_THEME_AMBER && theme_writes==0);
    long_ok(1100); assert(p.theme==HM_THEME_GREEN);
    menu(4,2200); click(HM_KEY_DOWN,3200); theme_ok=false; click(HM_KEY_OK,3300);
    assert(p.page==HM_PAGE_THEME && p.theme==HM_THEME_GREEN && p.notice==HM_NOTICE_THEME_FAILED);
    theme_ok=true; click(HM_KEY_DOWN,3400); click(HM_KEY_OK,3500);
    assert(p.page==HM_PAGE_MENU && !strcmp(stored_theme,"amber"));
    hm_product_init(&p,90,"ice_blue",(hm_product_store_t){0});
    assert(p.theme==HM_THEME_ICE_BLUE && hm_product_palette(p.theme)->primary==0x65DBFF);
    hm_product_init(&p,90,"obsolete",(hm_product_store_t){0}); assert(p.theme==HM_THEME_GREEN);
}
static void receive(uint32_t uid,const char *call,uint64_t t) {
    s.direction=HM_DIRECTION_SPEAKER; s.talker_active=true; s.talker_uid=uid;
    snprintf(s.talker_callsign,sizeof s.talker_callsign,"%s",call);
    hm_product_observe(&p,&s,true,true,false,t); hm_product_received_audio(&p,&s,t);
}
static void test_history(void) {
    setup(true); s.talker_active=true; s.talker_uid=7; strcpy(s.talker_callsign,"NO-AUDIO");
    hm_product_observe(&p,&s,true,true,false,10); assert(!p.history_count);
    receive(1,"SAME",100); receive(2,"SAME",200); receive(1,"SAME",300);
    assert(p.history_count==2 && p.history[0].uid==1 && p.history[1].uid==2);
    assert(hm_product_previous(&p)->uid==2);
    receive(3,"LONG-CALLSIGN-123456789012",400); receive(4,"FOUR",500);
    receive(5,"FIVE",600); receive(6,"SIX",700); assert(p.history_count==5 && p.history[4].uid==1);
    s.direction=HM_DIRECTION_MIC; hm_product_received_audio(&p,&s,800);
    assert(p.history[0].received_ms==700);
    char age[40]; hm_product_age(100,59999,age,sizeof age); assert(!strcmp(age,"刚刚"));
    hm_product_age(0,60000,age,sizeof age); assert(!strcmp(age,"1分钟前"));
    hm_product_age(0,7200000,age,sizeof age); assert(!strcmp(age,"2小时前"));
    hm_product_age(100,0,age,sizeof age); assert(!strcmp(age,"刚刚"));
    s.context_id=2; hm_product_observe(&p,&s,true,true,false,900); assert(!p.history_count);
    receive(7,"NEXT",1000); s.sid=2; hm_product_observe(&p,&s,true,true,false,1100); assert(!p.history_count);
    receive(8,"END",1200); s.connected=false; hm_product_observe(&p,&s,true,false,false,1300); assert(!p.history_count);
}
static void test_pairing_and_confirmations(void) {
    setup(false); assert(p.page==HM_PAGE_PAIR && p.pair_state==HM_PAIR_IDLE && !p.pair_window);
    assert(click(HM_KEY_OK,100)==HM_ACTION_PAIR_OPEN);
    hm_product_observe(&p,&s,false,false,true,200); assert(p.pair_state==HM_PAIR_WINDOW);
    assert(click(HM_KEY_DOWN,300)==HM_ACTION_PAIR_CLOSE); assert(p.page==HM_PAGE_MAIN && !p.bonded);
    menu(3,500); assert(p.page==HM_PAGE_PHONE); click(HM_KEY_OK,1400); assert(p.page==HM_PAGE_PAIR);
    click(HM_KEY_OK,1500); s.audio_ready=false; hm_product_observe(&p,&s,true,true,true,1600);
    assert(p.page==HM_PAGE_PAIR && p.pair_state==HM_PAIR_CONNECTING);
    assert(click(HM_KEY_DOWN,1700)==HM_ACTION_PAIR_CLOSE);
    assert(p.bonded && p.pair_state==HM_PAIR_SAVED && p.page==HM_PAGE_MAIN);
    setup(true); menu(3,100); click(HM_KEY_OK,1000);
    assert(p.page==HM_PAGE_REPLACE_CONFIRM && p.confirm_index==1);
    assert(!click(HM_KEY_OK,1100) && p.page==HM_PAGE_PHONE && p.bonded);
    click(HM_KEY_OK,1200); click(HM_KEY_UP,1300);
    assert(click(HM_KEY_OK,1400)==HM_ACTION_UNPAIR_AND_PAIR);
    hm_product_action_result(&p,HM_ACTION_UNPAIR_AND_PAIR,false,1410);
    assert(p.bonded && p.page==HM_PAGE_REPLACE_CONFIRM && p.notice==HM_NOTICE_UNPAIR_FAILED);
    assert(click(HM_KEY_OK,1500)==HM_ACTION_UNPAIR_AND_PAIR);
    hm_product_action_result(&p,HM_ACTION_UNPAIR_AND_PAIR,true,1510);
    assert(!p.bonded && p.page==HM_PAGE_PAIR);
    hm_product_observe(&p,&s,true,true,true,1600); assert(p.page==HM_PAGE_MAIN);
    setup(false); click(HM_KEY_OK,100); hm_product_observe(&p,&s,false,false,true,200);
    hm_product_observe(&p,&s,true,true,false,60100); /* binding survives timeout */
    assert(p.bonded && p.page==HM_PAGE_MAIN); /* completed hello is success */
    setup(false); click(HM_KEY_OK,100); hm_product_observe(&p,&s,false,false,true,200);
    s.audio_ready=false; hm_product_observe(&p,&s,true,true,false,60100);
    assert(p.bonded && p.page==HM_PAGE_PAIR && p.pair_state==HM_PAIR_SAVED);
    assert(click(HM_KEY_DOWN,60200)==HM_ACTION_PAIR_CLOSE && p.bonded);
    setup(false); click(HM_KEY_OK,100); hm_product_observe(&p,&s,false,false,true,200);
    hm_product_observe(&p,&s,false,false,false,60100); assert(p.pair_state==HM_PAIR_TIMEOUT);
    setup(false); click(HM_KEY_OK,100);
    hm_product_button(&p,HM_KEY_OK,HM_KEY_PRESS,200);
    assert(hm_product_tick(&p,1000)==HM_ACTION_PAIR_CLOSE && p.page==HM_PAGE_MAIN);
    hm_product_button(&p,HM_KEY_OK,HM_KEY_RELEASE,1010); hm_product_button(&p,HM_KEY_OK,HM_KEY_CLICK,1010);
    setup(true); menu(3,100); click(HM_KEY_DOWN,1000); click(HM_KEY_OK,1100);
    assert(p.page==HM_PAGE_REMOVE_CONFIRM && p.confirm_index==1);
    click(HM_KEY_OK,1200); assert(p.page==HM_PAGE_PHONE && p.bonded);
    click(HM_KEY_OK,1300); click(HM_KEY_UP,1400);
    assert(click(HM_KEY_OK,1500)==HM_ACTION_UNPAIR);
    hm_product_action_result(&p,HM_ACTION_UNPAIR,true,1510);
    assert(!p.bonded && p.page==HM_PAGE_PHONE && !p.pair_window);
    click(HM_KEY_OK,1600); assert(p.page==HM_PAGE_PAIR && !p.pair_window);
    assert(click(HM_KEY_OK,1700)==HM_ACTION_PAIR_OPEN);
    hm_product_action_result(&p,HM_ACTION_PAIR_OPEN,false,1710);
    assert(!p.pair_window && p.pair_state==HM_PAIR_FAILED);
}
static void test_real_release_adapter(void) {
    setup(true);p.page=HM_PAGE_MENU;p.menu_index=2;
    hm_product_button(&p,HM_KEY_OK,HM_KEY_PRESS,100);
    hm_product_button(&p,HM_KEY_OK,HM_KEY_LONG,600);
    hm_product_release_click(&p,HM_KEY_OK,700);assert(p.page==HM_PAGE_VOLUME);
    p.page=HM_PAGE_MENU;p.menu_index=0;
    for(unsigned i=0;i<2;i++){hm_product_button(&p,HM_KEY_DOWN,HM_KEY_PRESS,1000+i*100);hm_product_release_click(&p,HM_KEY_DOWN,1050+i*100);}
    assert(p.menu_index==2);
    hm_product_button(&p,HM_KEY_OK,HM_KEY_PRESS,2000);hm_product_release_click(&p,HM_KEY_OK,2800);
    assert(p.page==HM_PAGE_MAIN&&!p.ptt_down);
    hm_product_release_click(&p,HM_KEY_DOWN,3000);assert(!p.volume_dirty);
}
static void test_pair_code_generation(void) {
    hm_pair_code_t code={0};setup(false);p.page=HM_PAGE_PAIR;
    hm_product_pair_code_receive(&code,10,10,12034,true,false,true);
    assert(code.valid&&code.value==12034&&code.generation==10);
    hm_product_pair_code_sync(&code,11,true,false);assert(!code.valid);
    /* Failed uncommitted connection leaves the window waiting, without a code. */
    hm_product_observe(&p,&s,false,false,true,100);assert(p.pair_state==HM_PAIR_WINDOW&&!p.bonded);
    hm_product_pair_code_receive(&code,10,11,999999,true,false,true);assert(!code.valid);
    hm_product_pair_code_receive(&code,11,11,34,true,false,true);assert(code.valid&&code.value==34);
    hm_product_pair_code_sync(&code,11,true,true);assert(!code.valid);
    hm_product_observe(&p,&s,true,true,true,200);assert(p.bonded);
    s.connected=false;s.audio_ready=false;s.ready=false;
    hm_product_pair_code_sync(&code,12,false,false);
    hm_product_observe(&p,&s,true,false,false,300);assert(p.bonded&&p.pair_state==HM_PAIR_SAVED&&!code.valid);
    /* Closing or leaving the page rejects a late code; never modifies a bond. */
    hm_product_pair_code_receive(&code,12,12,654321,false,false,true);assert(!code.valid&&p.bonded);
    hm_product_pair_code_receive(&code,12,12,654321,true,false,false);assert(!code.valid&&p.bonded);
}
static void test_spectrum_setting(void) {
    setup(true); assert(!p.spectrum_enabled && HM_MENU_COUNT==7);
    hm_product_load_spectrum(&p,true); assert(p.spectrum_enabled && !spectrum_writes);
    hm_product_load_spectrum(&p,false); menu(5,100);
    assert(p.page==HM_PAGE_MENU && p.menu_index==5 && p.spectrum_enabled);
    assert(spectrum_writes==1 && stored_spectrum);
    spectrum_ok=false; click(HM_KEY_OK,1000);
    assert(p.spectrum_enabled && stored_spectrum && spectrum_writes==2);
    assert(p.notice==HM_NOTICE_SPECTRUM_FAILED);
    hm_product_tick(&p,3010); assert(p.notice==HM_NOTICE_NONE);
    spectrum_ok=true; click(HM_KEY_OK,3100);
    assert(!p.spectrum_enabled && !stored_spectrum && spectrum_writes==3);
    click(HM_KEY_DOWN,3200); assert(p.menu_index==6);
    click(HM_KEY_DOWN,3300); assert(p.menu_index==6);
    click(HM_KEY_OK,3400); assert(p.page==HM_PAGE_MAIN && !p.ptt_down);
    menu(5,4000); unsigned writes=spectrum_writes;
    long_ok(5000); assert(spectrum_writes==writes && !p.ptt_down);
    setup(true); p.store.store_spectrum=NULL; menu(5,100);
    assert(!p.spectrum_enabled && p.notice==HM_NOTICE_SPECTRUM_FAILED);
}
int main(void) {
    test_spectrum_setting(); test_navigation_and_ptt(); test_volume_transactions(); test_theme_transactions(); test_history(); test_pairing_and_confirmations(); test_real_release_adapter(); test_pair_code_generation();
    puts("handmic product tests PASS"); return 0;
}
