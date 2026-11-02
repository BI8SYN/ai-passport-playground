#include "hm_product.h"
#include <stdio.h>
#include <string.h>

static const hm_theme_palette_t palettes[HM_THEME_COUNT]={
    {"phosphor_green","荧光绿",0x020604,0x5EFF7D,0x389951,0x0E381A},
    {"amber","琥珀橙",0x080502,0xFFBC59,0xBA873F,0x4A3015},
    {"ice_blue","冰蓝",0x020609,0x65DBFF,0x4299B5,0x103C4A},
    {"soft_white","柔白",0x060707,0xDFEAE5,0x8D9D95,0x303E36}
};
const hm_theme_palette_t *hm_product_palette(hm_theme_t t) {
    return &palettes[(unsigned)t<HM_THEME_COUNT?(unsigned)t:0];
}
hm_theme_t hm_product_theme_from_id(const char *id) {
    for(unsigned i=0;id&&i<HM_THEME_COUNT;i++) if(!strcmp(id,palettes[i].id)) return (hm_theme_t)i;
    return HM_THEME_GREEN;
}
void hm_product_init(hm_product_t *p,uint8_t v,const char *id,hm_product_store_t store) {
    memset(p,0,sizeof *p); p->volume=p->saved_volume=v<=100?v:90;
    p->theme=p->saved_theme=hm_product_theme_from_id(id); p->store=store;
}
void hm_product_load_spectrum(hm_product_t *p,bool enabled) { p->spectrum_enabled=enabled; }
static void notice(hm_product_t *p,hm_product_notice_t n,uint64_t now) {
    p->notice=n; p->notice_until_ms=now+2000;
}
static bool settle_volume(hm_product_t *p,uint64_t now) {
    if(!p->volume_dirty) return true;
    bool ok=p->store.store_volume&&p->store.store_volume(p->volume,p->store.ctx);
    if(ok) p->saved_volume=p->volume;
    else { p->volume=p->saved_volume; notice(p,HM_NOTICE_VOLUME_FAILED,now); }
    p->volume_dirty=false; p->volume_due_ms=0; return ok;
}
static hm_product_actions_t home(hm_product_t *p) {
    hm_product_actions_t a=HM_ACTION_NONE;
    if(p->page==HM_PAGE_VOLUME) { p->volume=p->saved_volume; p->volume_dirty=false; }
    if(p->page==HM_PAGE_THEME) p->theme=p->saved_theme;
    if(p->page==HM_PAGE_PAIR) {
        a=HM_ACTION_PAIR_CLOSE;
        p->pair_window=false; p->pair_state=p->bonded?HM_PAIR_SAVED:HM_PAIR_IDLE;
    }
    p->page=HM_PAGE_MAIN;
    p->ok_blocked=p->key_down[HM_KEY_OK];
    return a;
}
void hm_product_observe(hm_product_t *p,const hm_snapshot_t *s,bool bonded,bool secure,bool window,uint64_t now) {
    if(!s) return;
    bool was_window=p->pair_window;
    if(p->observed&&(s->context_id!=p->core.context_id||s->sid!=p->core.sid||
       (p->core.connected&&!s->connected))) { p->history_count=0; memset(p->history,0,sizeof p->history); }
    if(p->observed&&p->core.connected&&!s->connected) p->disconnected_ms=now;
    if(!p->observed&&!s->connected) p->disconnected_ms=now;
    if(!p->observed&&!bonded) p->page=HM_PAGE_PAIR;
    p->core=*s; p->bonded=bonded; p->secure=secure; p->pair_window=window; p->observed=true;
    if(p->page==HM_PAGE_PAIR) {
        if(bonded&&secure&&s->audio_ready) { p->page=HM_PAGE_MAIN; p->pair_state=HM_PAIR_SAVED; p->ok_blocked=p->key_down[HM_KEY_OK]; }
        else if(bonded) p->pair_state=window?HM_PAIR_CONNECTING:HM_PAIR_SAVED;
        else if(window) p->pair_state=HM_PAIR_WINDOW;
        else if(was_window) p->pair_state=HM_PAIR_TIMEOUT;
    }
}
static void adjust_volume(hm_product_t *p,int delta,uint64_t now) {
    int v=(int)p->volume+delta; if(v<0)v=0; if(v>100)v=100;
    p->volume=(uint8_t)v; p->volume_dirty=p->volume!=p->saved_volume;
    p->volume_overlay_until_ms=now+1500;
    if(p->page!=HM_PAGE_VOLUME) p->volume_due_ms=now+500;
}
static unsigned phone_count(const hm_product_t *p) { return p->bonded?4:3; }
static void enter_menu_item(hm_product_t *p,uint64_t now) {
    switch(p->menu_index) {
    case 0:p->page=HM_PAGE_HISTORY; p->history_page=0; break;
    case 1:p->page=HM_PAGE_DETAIL; p->detail_page=0; break;
    case 2:settle_volume(p,now); p->page=HM_PAGE_VOLUME; break;
    case 3:p->page=HM_PAGE_PHONE; p->phone_index=0; break;
    case 4:p->page=HM_PAGE_THEME; p->theme=p->saved_theme; break;
    case 5:
        if(p->store.store_spectrum&&p->store.store_spectrum(!p->spectrum_enabled,p->store.ctx)) p->spectrum_enabled=!p->spectrum_enabled;
        else notice(p,HM_NOTICE_SPECTRUM_FAILED,now);
        break;
    default:(void)home(p); break;
    }
}
static hm_product_actions_t short_key(hm_product_t *p,hm_product_key_t k,uint64_t now) {
    int delta=k==HM_KEY_UP?-1:1;
    switch(p->page) {
    case HM_PAGE_MAIN:
        if(k!=HM_KEY_OK) adjust_volume(p,k==HM_KEY_UP?5:-5,now);
        break;
    case HM_PAGE_MENU:
        if(k==HM_KEY_OK) enter_menu_item(p,now);
        else if((delta<0&&p->menu_index)||(delta>0&&p->menu_index<HM_MENU_COUNT-1)) p->menu_index=(uint8_t)(p->menu_index+delta);
        break;
    case HM_PAGE_HISTORY:
        if(k==HM_KEY_OK) return home(p);
        /* One position per scroll permits renderers to use one/two-line callsigns. */
        if(delta<0&&p->history_page) p->history_page--;
        else if(delta>0&&p->history_page+1<p->history_count) p->history_page++;
        break;
    case HM_PAGE_DETAIL:
        if(k==HM_KEY_OK) return home(p);
        p->detail_page=k==HM_KEY_UP?0:1; break;
    case HM_PAGE_DEVICE:
        if(k==HM_KEY_OK) p->page=HM_PAGE_PHONE;
        else p->detail_page=k==HM_KEY_UP?0:1;
        break;
    case HM_PAGE_VOLUME:
        if(k!=HM_KEY_OK) adjust_volume(p,k==HM_KEY_UP?5:-5,now);
        else if(settle_volume(p,now)) p->page=HM_PAGE_MENU;
        break;
    case HM_PAGE_THEME:
        if(k==HM_KEY_OK) {
            if(p->theme==p->saved_theme||(p->store.store_theme&&p->store.store_theme(hm_product_palette(p->theme)->id,p->store.ctx))) {
                p->saved_theme=p->theme; p->page=HM_PAGE_MENU;
            } else { p->theme=p->saved_theme; notice(p,HM_NOTICE_THEME_FAILED,now); }
        } else if((delta<0&&p->theme>0)||(delta>0&&p->theme<HM_THEME_COUNT-1)) p->theme=(hm_theme_t)((int)p->theme+delta);
        break;
    case HM_PAGE_PHONE:
        if(k!=HM_KEY_OK) {
            if((delta<0&&p->phone_index)||(delta>0&&(unsigned)p->phone_index+1<phone_count(p))) p->phone_index=(uint8_t)(p->phone_index+delta);
        } else if(p->phone_index==phone_count(p)-1) p->page=HM_PAGE_MENU;
        else if(p->phone_index==(p->bonded?2:1)) { p->page=HM_PAGE_DEVICE; p->detail_page=0; }
        else if(!p->bonded) { p->page=HM_PAGE_PAIR; p->pair_state=HM_PAIR_IDLE; }
        else { p->page=p->phone_index==0?HM_PAGE_REPLACE_CONFIRM:HM_PAGE_REMOVE_CONFIRM; p->confirm_index=1; }
        break;
    case HM_PAGE_PAIR:
        if(k==HM_KEY_DOWN) return home(p);
        if(k==HM_KEY_OK&&!p->pair_window&&!p->bonded) {
            p->pair_state=HM_PAIR_WINDOW; p->pair_window=true; return HM_ACTION_PAIR_OPEN;
        }
        break;
    case HM_PAGE_REPLACE_CONFIRM:
    case HM_PAGE_REMOVE_CONFIRM:
        if(k!=HM_KEY_OK) p->confirm_index=k==HM_KEY_UP?0:1;
        else if(p->confirm_index) p->page=HM_PAGE_PHONE;
        else return p->page==HM_PAGE_REPLACE_CONFIRM?HM_ACTION_UNPAIR_AND_PAIR:HM_ACTION_UNPAIR;
        break;
    }
    return HM_ACTION_NONE;
}
hm_product_actions_t hm_product_button(hm_product_t *p,hm_product_key_t k,hm_product_key_event_t ev,uint64_t now) {
    if((unsigned)k>=3) return HM_ACTION_NONE;
    if(ev==HM_KEY_PRESS) {
        if(p->key_down[k]) return HM_ACTION_NONE;
        p->key_consumed[k]=false;
        p->key_down[k]=true; p->key_since_ms[k]=now; p->repeat_due_ms[k]=now+500;
        if(k==HM_KEY_OK&&p->page==HM_PAGE_MAIN&&!p->ok_blocked&&!p->key_consumed[k]) {
            p->ptt_down=true; return HM_ACTION_PTT_DOWN;
        }
    } else if(ev==HM_KEY_RELEASE) {
        p->key_down[k]=false;
        if(k==HM_KEY_OK) {
            p->ok_blocked=false;
            if(p->ptt_down) { p->ptt_down=false; p->key_consumed[k]=true; return HM_ACTION_PTT_UP; }
        }
    } else if(ev==HM_KEY_CLICK) {
        bool consumed=p->key_consumed[k]; p->key_consumed[k]=false;
        if(consumed) return HM_ACTION_NONE;
        return short_key(p,k,now);
    } else if(ev==HM_KEY_LONG) {
        /* BSP emits LONG after 500ms. Tick measures the product's 800ms OK. */
        return hm_product_tick(p,now);
    }
    return HM_ACTION_NONE;
}
hm_product_actions_t hm_product_tick(hm_product_t *p,uint64_t now) {
    if(p->notice!=HM_NOTICE_NONE&&now>=p->notice_until_ms) p->notice=HM_NOTICE_NONE;
    if(p->page!=HM_PAGE_VOLUME&&p->volume_dirty&&now>=p->volume_due_ms) settle_volume(p,now);
    if(p->key_down[HM_KEY_OK]&&!p->key_consumed[HM_KEY_OK]&&p->page!=HM_PAGE_MAIN&&
       now>=p->key_since_ms[HM_KEY_OK]&&now-p->key_since_ms[HM_KEY_OK]>=800) {
        p->key_consumed[HM_KEY_OK]=true; return home(p);
    }
    if(p->page==HM_PAGE_MAIN&&p->key_down[HM_KEY_UP]&&!p->key_consumed[HM_KEY_UP]&&
       now>=p->key_since_ms[HM_KEY_UP]&&now-p->key_since_ms[HM_KEY_UP]>=500) {
        p->key_consumed[HM_KEY_UP]=true;
        if(!p->key_down[HM_KEY_OK]&&!p->ptt_down&&!p->core.pending_ptt&&p->core.direction!=HM_DIRECTION_MIC) {
            settle_volume(p,now); p->page=HM_PAGE_MENU; p->menu_index=0;
        }
    }
    if(p->page==HM_PAGE_VOLUME) {
        for(unsigned k=0;k<2;k++) if(p->key_down[k]&&now>=p->repeat_due_ms[k]) {
            p->key_consumed[k]=true; adjust_volume(p,k==HM_KEY_UP?5:-5,now); p->repeat_due_ms[k]=now+150;
        }
    }
    return HM_ACTION_NONE;
}
void hm_product_action_result(hm_product_t *p,hm_product_actions_t a,bool success,uint64_t now) {
    if(a&(HM_ACTION_UNPAIR|HM_ACTION_UNPAIR_AND_PAIR)) {
        if(!success) { notice(p,HM_NOTICE_UNPAIR_FAILED,now); return; }
        p->bonded=false; p->secure=false; p->history_count=0;
        p->phone_index=0;
        if(a&HM_ACTION_UNPAIR_AND_PAIR) { p->page=HM_PAGE_PAIR; p->pair_window=true; p->pair_state=HM_PAIR_WINDOW; }
        else { p->page=HM_PAGE_PHONE; p->pair_window=false; p->pair_state=HM_PAIR_IDLE; }
    }
    if((a&HM_ACTION_PAIR_OPEN)&&!success) { p->pair_window=false; p->pair_state=HM_PAIR_FAILED; }
}
void hm_product_received_audio(hm_product_t *p,const hm_snapshot_t *s,uint64_t now) {
    if(!s||!s->connected||s->direction!=HM_DIRECTION_SPEAKER||!s->talker_active||!s->talker_callsign[0]||
       s->sid!=p->core.sid||s->context_id!=p->core.context_id) return;
    unsigned at=0;
    for(;at<p->history_count;at++) if(p->history[at].uid==s->talker_uid&&!strcmp(p->history[at].callsign,s->talker_callsign)) break;
    if(at==p->history_count&&p->history_count<HM_HISTORY_MAX) p->history_count++;
    if(at>=HM_HISTORY_MAX) at=HM_HISTORY_MAX-1;
    memmove(p->history+1,p->history,at*sizeof p->history[0]);
    p->history[0].uid=s->talker_uid;
    snprintf(p->history[0].callsign,sizeof p->history[0].callsign,"%s",s->talker_callsign);
    p->history[0].received_ms=now;
}
const hm_history_entry_t *hm_product_previous(const hm_product_t *p) {
    for(unsigned i=0;i<p->history_count;i++) {
        if(p->core.talker_active&&p->core.direction==HM_DIRECTION_SPEAKER&&
           p->history[i].uid==p->core.talker_uid&&!strcmp(p->history[i].callsign,p->core.talker_callsign)) continue;
        return &p->history[i];
    }
    return NULL;
}
void hm_product_age(uint64_t ms,uint64_t now,char *out,size_t n) {
    uint64_t age=now>=ms?(now-ms)/60000:0;
    if(age==0) snprintf(out,n,"刚刚");
    else if(age<60) snprintf(out,n,"%llu分钟前",(unsigned long long)age);
    else snprintf(out,n,"%llu小时前",(unsigned long long)(age/60));
}
bool hm_product_volume_busy(const hm_product_t *p) { return p->page==HM_PAGE_VOLUME||p->volume_dirty; }
void hm_product_remote_volume(hm_product_t *p,uint8_t v) {
    if(v>100||hm_product_volume_busy(p)) return;
    p->volume=p->saved_volume=v; p->volume_dirty=false; p->volume_due_ms=0;
}

hm_product_actions_t hm_product_release_click(hm_product_t *p,hm_product_key_t key,uint64_t now) {
    if((unsigned)key>=3||!p->key_down[key])return HM_ACTION_NONE;
    hm_product_actions_t a=hm_product_tick(p,now);
    a|=hm_product_button(p,key,HM_KEY_RELEASE,now);
    a|=hm_product_button(p,key,HM_KEY_CLICK,now);return a;
}

void hm_product_pair_code_sync(hm_pair_code_t *code,uint32_t gen,bool window,bool secure) {
    if(code->generation!=gen||!window||secure) code->valid=false;
    code->generation=gen;
}
void hm_product_pair_code_receive(hm_pair_code_t *code,uint32_t event_gen,uint32_t gen,uint32_t value,bool window,bool secure,bool pair_page) {
    hm_product_pair_code_sync(code,gen,window,secure);
    if(event_gen==gen&&window&&!secure&&pair_page) { code->value=value;code->valid=true; }
}
