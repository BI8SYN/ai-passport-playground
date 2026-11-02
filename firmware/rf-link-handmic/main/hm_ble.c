/* RF Link Handmic 1.0 transport. No business JSON parsing and no audio ownership. */
#include "hm_ble.h"
#include "hm_fragment.h"
#include "hm_cccd_guard.h"
#include "hm_tx_guard.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define UUID_BYTES(n) 0x4b,0x4e,0x49,0x4c,0x4f,0x4d,0x5f,0xa1,0x9f,0x4d,0x9b,0x7a,n,0,0x3a,0x9c
#define UUID(n) static const ble_uuid128_t uuid_##n=BLE_UUID128_INIT(UUID_BYTES(n))
UUID(1);UUID(2);UUID(3);UUID(4);UUID(5);UUID(6);UUID(7);UUID(8);UUID(9);
#define WRITE_SECURITY (BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN|BLE_GATT_CHR_F_WRITE_AUTHOR)
#define READ_SECURITY (BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN|BLE_GATT_CHR_F_READ_AUTHOR)
#define CCCD_SECURITY (BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC|BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN|BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHOR)
#define CONTROL_SLOTS 4
static const char *TAG="hm-ble";
static hm_ble_callbacks_t cb;
static _Atomic uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static _Atomic uint32_t generation=1;
static _Atomic uint16_t max_value=20;
static _Atomic bool authenticated, bonded, window_open, initialized, host_ready;
static _Atomic uint32_t window_until;
static _Atomic bool unpair_requested, pairing_changed;
static _Atomic int unpair_result=-2;
static uint8_t own_addr_type;
static char device_name[24];
static ble_addr_t bond_identity;
static bool bond_valid, store_fault, store_io_fault;
static uint16_t control_handle,mic_handle,flow_handle;
static bool sub_control,sub_mic,sub_flow,had_security;
static bool indication_pending;
static uint16_t pending_count;
static uint32_t pending_generation;
static uint32_t next_transfer=1;
static hm_fragment_t rx;
static struct ble_npl_event pump_event;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static char info_json[385]="{}",status_json[513]="{}";
static uint16_t info_len=2,status_len=2;

typedef struct {
    bool used,started;
    uint32_t generation,transfer,order;
    uint16_t len,offset,limit;
    hm_ble_priority_t priority;
    uint8_t bytes[512];
} control_packet_t;
static control_packet_t controls[CONTROL_SLOTS];
static uint32_t enqueue_order;
static int active_control=-1;
typedef struct { uint32_t generation,audio_epoch;uint64_t at_ms;uint16_t len;uint8_t bytes[100]; } data_packet_t;
static QueueHandle_t audio_queue,flow_queue;
static SemaphoreHandle_t audio_lock;
static hm_tx_guard_t tx_guard;
static uint64_t audio_tick;
static unsigned audio_budget;
static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time()/1000; }
static int gap_event(struct ble_gap_event *e,void *arg);
static void kick(void) {
    if (atomic_load(&host_ready)) ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&pump_event);
}
static void refresh_bond(void) {
    ble_addr_t ids[2];int n=0;
    int rc=ble_store_util_bonded_peers(ids,&n,2);
    bond_valid=!rc && n==1;
    /* Never select/evict an arbitrary peer if a previous installation held >1. */
    store_fault=store_io_fault || rc!=0 || n>1;
    if (bond_valid) bond_identity=ids[0];
    atomic_store(&bonded,bond_valid || store_fault);
}
static bool same_identity(const ble_addr_t *a,const ble_addr_t *b) {
    return a->type==b->type && !memcmp(a->val,b->val,sizeof(a->val));
}
static bool trusted(uint16_t conn) {
    struct ble_gap_conn_desc d;
    if (atomic_load(&unpair_requested) || store_fault || !bond_valid || ble_gap_conn_find(conn,&d)) return false;
    if (!d.sec_state.encrypted || !d.sec_state.authenticated || !d.sec_state.bonded || d.sec_state.key_size!=16 || !same_identity(&d.peer_id_addr,&bond_identity)) return false;
    struct ble_store_key_sec key={.peer_addr=d.peer_id_addr};struct ble_store_value_sec val;
    return !ble_store_read_our_sec(&key,&val) && val.authenticated && val.sc && val.key_size==16;
}
static bool window_valid(void) {
    return atomic_load(&window_open) && (int32_t)(atomic_load(&window_until)-(uint32_t)now_ms())>0;
}
static bool can_pair(void) {
    return window_valid() && !atomic_load(&bonded) && !store_fault && !atomic_load(&unpair_requested);
}
static void discard_queued_audio(void) {
    data_packet_t packet;uint32_t count=0;
    /* Receive counts actual removed packets, so concurrent security invalidation
     * and runtime clear cannot both count the same queue entry. */
    while (xQueueReceive(audio_queue,&packet,0)==pdTRUE) count++;
    hm_tx_guard_drop(&tx_guard,count);
}
static void clear_transport(void) {
    hm_fragment_reset(&rx);
    portENTER_CRITICAL(&mux);
    memset(controls,0,sizeof(controls));active_control=-1;
    portEXIT_CRITICAL(&mux);
    hm_tx_guard_revoke(&tx_guard);discard_queued_audio();xQueueReset(flow_queue);
}
static void report_link(void) {
    if (cb.link) cb.link(atomic_load(&generation),atomic_load(&connection)!=BLE_HS_CONN_HANDLE_NONE,atomic_load(&authenticated),atomic_load(&max_value));
}
static void invalidate(void) {
    atomic_store(&authenticated,false);atomic_fetch_add(&generation,1);clear_transport();report_link();
}
static void update_link(void) {
    uint16_t conn=atomic_load(&connection);
    bool sec=conn!=BLE_HS_CONN_HANDLE_NONE && trusted(conn);
    bool ready=sec && sub_control && sub_mic && sub_flow;
    if ((!sec && had_security) || (!ready && atomic_load(&authenticated))) invalidate();
    had_security=sec;
    bool changed=ready!=atomic_load(&authenticated);
    atomic_store(&authenticated,ready);
    if (changed) report_link();
}
/* No round-robin callback: overflow must fail instead of deleting a bond. */
static int store_status(struct ble_store_status_event *event,void *arg) {
    (void)event;(void)arg;return BLE_HS_ESTORE_CAP;
}
/* A final authorization gate at persistence, including peers choosing JustWorks.
 * A single pairing writes both OUR_SEC and PEER_SEC; bond_identity is refreshed
 * only after its encryption-complete event so both writes share one decision. */
static int store_write(int type,const union ble_store_value *v) {
    if (type==BLE_STORE_OBJ_TYPE_OUR_SEC || type==BLE_STORE_OBJ_TYPE_PEER_SEC) {
        if (!v->sec.authenticated || !v->sec.sc || v->sec.key_size!=16) return BLE_HS_EAUTHEN;
        if (bond_valid) {
            if (!same_identity(&bond_identity,&v->sec.peer_addr)) return BLE_HS_EAUTHEN;
        } else if (!can_pair()) return BLE_HS_EAUTHEN;
    }
    int rc=ble_store_config_write(type,v);
    if (rc) { store_io_fault=true;store_fault=true;atomic_store(&bonded,true); }
    return rc;
}
static int advertise(void) {
    if (!atomic_load(&host_ready) || atomic_load(&connection)!=BLE_HS_CONN_HANDLE_NONE) return 0;
    struct ble_hs_adv_fields f={0};
    f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128=(ble_uuid128_t *)&uuid_1;f.num_uuids128=1;f.uuids128_is_complete=1;
    int rc=ble_gap_adv_set_fields(&f);if (rc) return rc;
    struct ble_hs_adv_fields scan={0};scan.name=(uint8_t *)device_name;scan.name_len=strlen(device_name);scan.name_is_complete=1;
    rc=ble_gap_adv_rsp_set_fields(&scan);if (rc) return rc;
    struct ble_gap_adv_params p={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN};
    return ble_gap_adv_start(own_addr_type,NULL,BLE_HS_FOREVER,&p,gap_event,NULL);
}
static void stop_untrusted(void) {
    uint16_t conn=atomic_load(&connection);
    if (conn!=BLE_HS_CONN_HANDLE_NONE && !trusted(conn)) (void)ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);
}
static int access_gatt(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctxt,void *arg) {
    (void)attr;unsigned kind=(unsigned)(uintptr_t)arg;
    if (kind!=2 && !trusted(conn)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR && (kind==2 || kind==9)) {
        char value[513];uint16_t len;
        portENTER_CRITICAL(&mux);
        len=kind==2?info_len:status_len;memcpy(value,kind==2?info_json:status_json,len);
        portEXIT_CRITICAL(&mux);
        return os_mbuf_append(ctxt->om,value,len)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    }
    if (ctxt->op!=BLE_GATT_ACCESS_OP_WRITE_CHR || (kind!=3 && kind!=6 && kind!=8)) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    if (!atomic_load(&authenticated)) return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    uint8_t value[244];uint16_t len=0;
    if (OS_MBUF_PKTLEN(ctxt->om)>sizeof(value) || ble_hs_mbuf_to_flat(ctxt->om,value,sizeof(value),&len)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint32_t gen=atomic_load(&generation);
    if (kind==3) {
        hm_fragment_result_t result=hm_fragment_push(&rx,value,len,now_ms());
        if (result==HM_FRAGMENT_BAD) {
            if (cb.fragment_error) cb.fragment_error(gen);
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (result==HM_FRAGMENT_COMPLETE && cb.control) cb.control(gen,rx.bytes,rx.used);
    } else if (kind==6) {
        if (len!=100) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (cb.audio) cb.audio(gen,value,len);
    } else {
        if (len!=24) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (cb.flow) cb.flow(gen,value,len);
    }
    return 0;
}
#define CHR(n,flags_,handle_) {.uuid=&uuid_##n.u,.access_cb=access_gatt,.arg=(void *)(uintptr_t)n,.flags=(flags_),.val_handle=(handle_)}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&uuid_1.u,.characteristics=(struct ble_gatt_chr_def[]){
        CHR(2,BLE_GATT_CHR_F_READ,NULL),
        CHR(3,BLE_GATT_CHR_F_WRITE|WRITE_SECURITY,NULL),
        CHR(4,BLE_GATT_CHR_F_INDICATE|CCCD_SECURITY,&control_handle),
        CHR(5,BLE_GATT_CHR_F_NOTIFY|CCCD_SECURITY,&mic_handle),
        CHR(6,BLE_GATT_CHR_F_WRITE_NO_RSP|WRITE_SECURITY,NULL),
        CHR(7,BLE_GATT_CHR_F_NOTIFY|CCCD_SECURITY,&flow_handle),
        CHR(8,BLE_GATT_CHR_F_WRITE_NO_RSP|WRITE_SECURITY,NULL),
        CHR(9,BLE_GATT_CHR_F_READ|READ_SECURITY,NULL),{0}}},{0}};
static void indication_complete(int status) {
    if (!indication_pending) return;
    indication_pending=false;
    if (pending_generation!=atomic_load(&generation)) return;
    if (status!=BLE_HS_EDONE) {
        invalidate();(void)ble_gap_terminate(atomic_load(&connection),BLE_ERR_REM_USER_CONN_TERM);return;
    }
    portENTER_CRITICAL(&mux);
    if (active_control>=0) {
        control_packet_t *p=&controls[active_control];p->offset+=pending_count;
        if (p->offset==p->len) { p->used=false;active_control=-1; }
    }
    portEXIT_CRITICAL(&mux);kick();
}
static int gap_event(struct ble_gap_event *e,void *arg) {
    (void)arg;
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (e->connect.status) { (void)advertise();break; }
        atomic_store(&connection,e->connect.conn_handle);atomic_fetch_add(&generation,1);
        atomic_store(&max_value,20);sub_control=sub_mic=sub_flow=had_security=false;indication_pending=false;
        clear_transport();refresh_bond();report_link();
        { struct ble_gap_upd_params p={.itvl_min=6,.itvl_max=12,.latency=0,.supervision_timeout=200};
          (void)ble_gap_update_params(e->connect.conn_handle,&p); }
        /* A peer may read Info unencrypted. Only an open physical window asks
         * an unknown peer to pair; a bond reconnect requests key restoration. */
        if (bond_valid || can_pair()) (void)ble_gap_security_initiate(e->connect.conn_handle);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        atomic_store(&connection,BLE_HS_CONN_HANDLE_NONE);sub_control=sub_mic=sub_flow=had_security=false;
        indication_pending=false;atomic_store(&max_value,20);invalidate();refresh_bond();(void)advertise();break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        refresh_bond();update_link();
        if (trusted(e->enc_change.conn_handle)) { atomic_store(&window_open,false);atomic_store(&window_until,0); }
        else stop_untrusted();
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (!can_pair() || e->passkey.params.action!=BLE_SM_IOACT_DISP) { stop_untrusted();break; }
        { struct ble_sm_io io={.action=BLE_SM_IOACT_DISP,.passkey=esp_random()%1000000};
          if (cb.passkey) cb.passkey(atomic_load(&generation),io.passkey);
          if (ble_sm_inject_io(e->passkey.conn_handle,&io)) stop_untrusted(); }
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Repair is only available after local explicit unpair; never delete. */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_AUTHORIZE:
        e->authorize.out_response=trusted(e->authorize.conn_handle)?BLE_GAP_AUTHORIZE_ACCEPT:BLE_GAP_AUTHORIZE_REJECT;
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (e->subscribe.attr_handle==control_handle) sub_control=e->subscribe.cur_indicate;
        if (e->subscribe.attr_handle==mic_handle) sub_mic=e->subscribe.cur_notify;
        if (e->subscribe.attr_handle==flow_handle) sub_flow=e->subscribe.cur_notify;
        update_link();break;
    case BLE_GAP_EVENT_MTU:
        { uint16_t n=ble_att_mtu(e->mtu.conn_handle);n=n>3?n-3:20;
          atomic_store(&max_value,n>244?244:n);report_link(); }break;
    case BLE_GAP_EVENT_NOTIFY_TX:
        if (e->notify_tx.attr_handle==control_handle && e->notify_tx.indication && e->notify_tx.status!=0) indication_complete(e->notify_tx.status);
        /* Notification completion never pumps audio immediately: 10ms budget
         * belongs to poll's clock so a burst cannot consume all 32 credits. */
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (atomic_load(&connection)==BLE_HS_CONN_HANDLE_NONE) (void)advertise();
        break;
    default:break;
    }
    return 0;
}
static void pump_control(void) {
    if (indication_pending) return;
    uint8_t value[244];uint16_t count=0;uint32_t gen=atomic_load(&generation);
    portENTER_CRITICAL(&mux);
    if (active_control<0) {
        for (int i=0;i<CONTROL_SLOTS;i++) {
            if (!controls[i].used) continue;
            if (controls[i].generation!=gen) { controls[i].used=false;continue; }
            if (active_control<0 || controls[i].priority<controls[active_control].priority ||
                (controls[i].priority==controls[active_control].priority && (int32_t)(controls[i].order-controls[active_control].order)<0)) active_control=i;
        }
    }
    if (active_control>=0) {
        control_packet_t *p=&controls[active_control];
        uint16_t limit=p->limit<atomic_load(&max_value)?p->limit:atomic_load(&max_value);
        size_t encoded=hm_fragment_encode(value,sizeof(value),p->transfer,p->offset,p->bytes,p->len,limit);
        count=encoded?(uint16_t)(encoded-8):0;
        p->started=true;
    }
    portEXIT_CRITICAL(&mux);
    if (!count) return;
    struct os_mbuf *om=ble_hs_mbuf_from_flat(value,count+8);if (!om) return;
    /* Mark before calling: NimBLE may deliver its 'sent' event synchronously. */
    indication_pending=true;pending_generation=gen;pending_count=count;
    int rc=ble_gatts_indicate_custom(atomic_load(&connection),control_handle,om);
    if (rc) indication_pending=false; /* mbuf is consumed on every return path */
}
static bool notify_packet(uint16_t handle,const data_packet_t *p) {
    if (p->generation!=atomic_load(&generation)) return true;
    struct os_mbuf *om=ble_hs_mbuf_from_flat(p->bytes,p->len);
    return om && !ble_gatts_notify_custom(atomic_load(&connection),handle,om);
}
typedef enum { AUDIO_SUBMITTED, AUDIO_DROPPED, AUDIO_FAILED } audio_submit_t;
static audio_submit_t submit_audio(const data_packet_t *packet) {
    struct os_mbuf *om=ble_hs_mbuf_from_flat(packet->bytes,packet->len);
    if (!om) { hm_tx_guard_result(&tx_guard,false);return AUDIO_FAILED; }
    /* This is the final local admission check, after allocation and directly
     * before NimBLE takes ownership. Already submitted/controller packets cannot
     * be recalled when the physical button changes afterward. */
    if (!hm_tx_guard_submit_allowed(&tx_guard,packet->audio_epoch)) { os_mbuf_free_chain(om);return AUDIO_DROPPED; }
    int rc=ble_gatts_notify_custom(atomic_load(&connection),mic_handle,om);
    hm_tx_guard_result(&tx_guard,rc==0);
    return rc?AUDIO_FAILED:AUDIO_SUBMITTED;
}
static void pump(struct ble_npl_event *event) {
    (void)event;uint64_t now=now_ms();
    bool unpaired=atomic_exchange(&unpair_requested,false);
    if (unpaired) {
        /* unpair() closed the old window synchronously. A later physical
         * pairing(true) request may already have opened a replacement window. */
        invalidate();
        uint16_t conn=atomic_load(&connection);
        if (conn!=BLE_HS_CONN_HANDLE_NONE) (void)ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);
        int rc=ble_store_clear();
        if (!rc) store_io_fault=false;
        refresh_bond();
        if (rc) {
            store_fault=true;atomic_store(&bonded,true);atomic_store(&window_open,false);
            ESP_LOGE(TAG,"Bond clear failed: %d",rc);
        }
        atomic_store(&unpair_result,rc?-1:1);
    }
    if (atomic_load(&window_open) && !window_valid()) {
        atomic_store(&window_open,false);stop_untrusted();
    }
    if (atomic_exchange(&pairing_changed,false)) {
        uint16_t conn=atomic_load(&connection);
        if (can_pair() && !unpaired && conn!=BLE_HS_CONN_HANDLE_NONE) (void)ble_gap_security_initiate(conn);
        else if (!atomic_load(&window_open)) stop_untrusted();
    }
    if (hm_fragment_expire(&rx,now) && cb.fragment_error) cb.fragment_error(atomic_load(&generation));
    update_link();
    if (!atomic_load(&authenticated)) { discard_queued_audio();return; }
    pump_control();
    data_packet_t packet;
    if (xQueuePeek(flow_queue,&packet,0)==pdTRUE && notify_packet(flow_handle,&packet)) (void)xQueueReceive(flow_queue,&packet,0);
    uint64_t tick=now/10;
    if (tick!=audio_tick) { audio_tick=tick;audio_budget=2; }
    while (audio_budget) {
        /* Runtime clear synchronizes with this submission boundary. Host never
         * waits for the runtime; queued frames stay queued on lock contention. */
        if (xSemaphoreTake(audio_lock,0)!=pdTRUE) break;
        if (xQueueReceive(audio_queue,&packet,0)!=pdTRUE) { xSemaphoreGive(audio_lock);break; }
        /* Take packet age after dequeue: another task may enqueue after the
         * pump's earlier timestamp, which must not wrap unsigned subtraction. */
        uint64_t packet_now=now_ms();
        bool stale=packet.audio_epoch!=hm_tx_guard_epoch(&tx_guard) || packet.generation!=atomic_load(&generation) || hm_tx_guard_expired(packet.at_ms,packet_now);
        audio_submit_t result=AUDIO_DROPPED;
        if (stale) hm_tx_guard_drop(&tx_guard,1);
        else result=submit_audio(&packet);
        xSemaphoreGive(audio_lock);
        if (result==AUDIO_FAILED) break; /* backpressure drops, never retries stale PCM */
        if (result==AUDIO_SUBMITTED) audio_budget--;
    }
}
static void synced(void) {
    int rc=ble_hs_util_ensure_addr(0);if (!rc) rc=ble_hs_id_infer_auto(0,&own_addr_type);
    refresh_bond();
    /* No silent fallback if the required linker wrap was omitted. */
    if (!hm_cccd_guard_verified()) { ESP_LOGE(TAG,"CCCD registration guard missing; BLE disabled");return; }
    atomic_store(&host_ready,true);if (!rc) rc=advertise();
    if (rc) ESP_LOGE(TAG,"BLE synchronization/advertisement: %d",rc);
    kick();
}
static void reset(int reason) {
    ESP_LOGE(TAG,"BLE host reset: %d",reason);atomic_store(&host_ready,false);
    atomic_store(&connection,BLE_HS_CONN_HANDLE_NONE);sub_control=sub_mic=sub_flow=had_security=false;indication_pending=false;invalidate();
}
static void host_task(void *unused) { (void)unused;nimble_port_run();vTaskDelete(NULL); }
esp_err_t hm_ble_init(const char *device_id,const hm_ble_callbacks_t *callbacks) {
    if (!device_id || !callbacks || atomic_load(&initialized)) return ESP_ERR_INVALID_ARG;
    cb=*callbacks;size_t n=strlen(device_id);
    snprintf(device_name,sizeof(device_name),"RF Link · %.4s",n>=4?device_id+n-4:device_id);
    audio_queue=xQueueCreate(6,sizeof(data_packet_t));flow_queue=xQueueCreate(1,sizeof(data_packet_t));
    audio_lock=xSemaphoreCreateMutex();
    if (!audio_queue || !flow_queue || !audio_lock) return ESP_ERR_NO_MEM;
    esp_err_t rc=nimble_port_init();if (rc!=ESP_OK) return rc;
    /* The ESP-IDF example API deliberately declares this init externally. */
    extern void ble_store_config_init(void);
    ble_store_config_init();
    ble_hs_cfg.store_read_cb=ble_store_config_read;ble_hs_cfg.store_write_cb=store_write;ble_hs_cfg.store_delete_cb=ble_store_config_delete;
    ble_hs_cfg.store_status_cb=store_status;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_ONLY;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_mitm=1;
    ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_sc_only=1;ble_hs_cfg.sm_sec_lvl=4;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sync_cb=synced;ble_hs_cfg.reset_cb=reset;
    ble_svc_gap_init();ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set(device_name) || ble_att_set_preferred_mtu(247) || ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) return ESP_FAIL;
    ble_npl_event_init(&pump_event,pump,NULL);atomic_store(&initialized,true);
    if (xTaskCreate(host_task,"hm_ble",5120,NULL,5,NULL)!=pdPASS) { atomic_store(&initialized,false);return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
void hm_ble_pairing(bool enabled) {
    if (!atomic_load(&initialized)) return;
    /* Existing bond stays in place; opening a window cannot authorize a swap. */
    atomic_store(&window_until,enabled?(uint32_t)now_ms()+60000:0);atomic_store(&window_open,enabled);
    atomic_store(&pairing_changed,true);kick();
}
void hm_ble_unpair(void) {
    if (!atomic_load(&initialized)) return;
    atomic_store(&window_open,false);atomic_store(&window_until,0);
    atomic_store(&unpair_result,0);atomic_store(&unpair_requested,true);atomic_store(&authenticated,false);kick();
}
void hm_ble_poll(uint64_t now) { (void)now;kick(); }
bool hm_ble_send_control_priority(uint32_t gen,const char *json,size_t len,uint16_t limit,hm_ble_priority_t priority) {
    if (!json || !len || len>512 || limit<9 || limit>244 || priority>HM_BLE_DISPLAY || !atomic_load(&authenticated) || gen!=atomic_load(&generation)) return false;
    bool accepted=false;
    portENTER_CRITICAL(&mux);
    int slot=-1;
    for (int i=0;i<CONTROL_SLOTS;i++) if (!controls[i].used) { slot=i;break; }
    /* Urgent safety messages may evict an unsent lower priority item; a started
     * transfer is never interleaved or replaced. */
    if (slot<0) for (int i=0;i<CONTROL_SLOTS;i++) if (i!=active_control && !controls[i].started && controls[i].priority>priority) { slot=i;break; }
    if (slot>=0 && gen==atomic_load(&generation) && atomic_load(&authenticated)) {
        control_packet_t *p=&controls[slot];
        *p=(control_packet_t){.used=true,.generation=gen,.transfer=next_transfer++,.order=enqueue_order++,.len=len,.limit=limit,.priority=priority};
        memcpy(p->bytes,json,len);accepted=true;
    }
    portEXIT_CRITICAL(&mux);if (accepted) kick();return accepted;
}
bool hm_ble_send_control(uint32_t gen,const char *json,size_t len,uint16_t limit) { return hm_ble_send_control_priority(gen,json,len,limit,HM_BLE_RESPONSE); }
static bool enqueue_data(QueueHandle_t q,uint32_t gen,const uint8_t *data,size_t len,bool overwrite) {
    if (!data || !atomic_load(&authenticated) || gen!=atomic_load(&generation) || len>atomic_load(&max_value)) return false;
    data_packet_t p={.generation=gen,.audio_epoch=hm_tx_guard_epoch(&tx_guard),.at_ms=now_ms(),.len=len};memcpy(p.bytes,data,len);
    bool ok=(overwrite?xQueueOverwrite(q,&p):xQueueSend(q,&p,0))==pdTRUE;
    if (ok && overwrite) kick();
    return ok;
}
bool hm_ble_notify_audio(uint32_t gen,const uint8_t *bytes,size_t len) {
    if ((len!=100) || !audio_lock || xSemaphoreTake(audio_lock,0)!=pdTRUE) return false;
    bool ok=enqueue_data(audio_queue,gen,bytes,len,false);
    xSemaphoreGive(audio_lock);
    if (ok) kick();
    return ok;
}
void hm_ble_clear_audio(uint32_t gen) {
    if (!audio_lock || gen!=atomic_load(&generation)) return;
    /* Runtime task only: this waits just for a local nonblocking NimBLE submit,
     * never for controller completion, an indication ACK or a host event. */
    xSemaphoreTake(audio_lock,portMAX_DELAY);
    if (gen==atomic_load(&generation)) { hm_tx_guard_revoke(&tx_guard);discard_queued_audio(); }
    xSemaphoreGive(audio_lock);
}
bool hm_ble_notify_flow(uint32_t gen,const uint8_t *bytes,size_t len) { return len==24 && enqueue_data(flow_queue,gen,bytes,len,true); }
static bool set_json(char *dest,uint16_t *length,size_t maximum,const char *json) {
    if (!json) return false;
    size_t len=strnlen(json,maximum+1);
    if (len>maximum) return false;
    portENTER_CRITICAL(&mux);memcpy(dest,json,len+1);*length=len;portEXIT_CRITICAL(&mux);return true;
}
bool hm_ble_set_info(const char *json) { return set_json(info_json,&info_len,384,json); }
bool hm_ble_set_status(const char *json) { return set_json(status_json,&status_len,512,json); }
bool hm_ble_bonded(void) { return atomic_load(&bonded); }
bool hm_ble_connected(void) { return atomic_load(&connection)!=BLE_HS_CONN_HANDLE_NONE; }
bool hm_ble_authenticated(void) { return atomic_load(&authenticated); }
bool hm_ble_pairing_open(void) { return window_valid(); }
uint32_t hm_ble_generation(void) { return atomic_load(&generation); }
uint16_t hm_ble_max_value(void) { return atomic_load(&max_value); }

void hm_ble_set_audio_gate(const _Atomic bool *permitted) { hm_tx_guard_set(&tx_guard,permitted); }
void hm_ble_revoke_audio(void) { hm_tx_guard_revoke(&tx_guard); }
void hm_ble_get_audio_stats(uint32_t *submitted,uint32_t *dropped) { hm_tx_guard_stats(&tx_guard,submitted,dropped); }

int hm_ble_unpair_result(void){return atomic_load(&unpair_result);}

bool hm_ble_ready(void){return atomic_load(&host_ready);}
