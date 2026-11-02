/* Disposable BLE transport/binding experiment. No certificate enrollment or eFuse writes. */
#include "lab_model.h"
#include "audio_lab.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "sodium.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

#define WIRE_MAX 512
/* Browser UUIDs: 3fc10001/2/3-8b80-4f3a-9b22-4a4700000001 */
#define UUID_BYTES(n) 0x01,0,0,0,0x47,0x4a,0x22,0x9b,0x3a,0x4f,0x80,0x8b,n,0,0xc1,0x3f
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(UUID_BYTES(1));
static const ble_uuid128_t rx_uuid=BLE_UUID128_INIT(UUID_BYTES(2));
static const ble_uuid128_t tx_uuid=BLE_UUID128_INIT(UUID_BYTES(3));
static const ble_uuid128_t changed_uuid=BLE_UUID128_INIT(UUID_BYTES(4));
static const ble_uuid128_t audio_control_uuid=BLE_UUID128_INIT(UUID_BYTES(5));
static const ble_uuid128_t audio_rx_uuid=BLE_UUID128_INIT(UUID_BYTES(6));
static const ble_uuid128_t audio_tx_uuid=BLE_UUID128_INIT(UUID_BYTES(7));
static const ble_uuid128_t audio_stats_uuid=BLE_UUID128_INIT(UUID_BYTES(8));
static lab_model_t state;
static const char *TAG="ble-signer-lab";
static char view[WIRE_MAX],rx[WIRE_MAX];
static size_t rx_used;
static bool rx_discard;
static SemaphoreHandle_t view_lock;
static QueueHandle_t events;
static uint8_t addr_type;
static _Atomic uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static _Atomic uint32_t epoch;
static uint16_t changed_handle;
static uint32_t worker_epoch;
static _Atomic bool subscribed;
static bool erase_confirm;
static const char *note="Ready; open Web signer";
static lv_obj_t *ui_state,*ui_key,*ui_stats,*ui_help,*ui_battery;
static uint64_t now_ms(void){return (uint64_t)esp_timer_get_time()/1000;}
typedef struct {uint32_t epoch;int button;char line[WIRE_MAX];} event_t;
static int gap_event(struct ble_gap_event *event,void *arg);

static int advertise(void){
    struct ble_hs_adv_fields f={0};
    f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128=(ble_uuid128_t *)&service_uuid;f.num_uuids128=1;f.uuids128_is_complete=1;
    int rc=ble_gap_adv_set_fields(&f);if(rc)return rc;
    struct ble_hs_adv_fields scan={0};scan.name=(const uint8_t *)"Passport Sign Lab";
    scan.name_len=strlen((const char *)scan.name);scan.name_is_complete=1;
    rc=ble_gap_adv_rsp_set_fields(&scan);if(rc)return rc;
    struct ble_gap_adv_params p={0};p.conn_mode=BLE_GAP_CONN_MODE_UND;p.disc_mode=BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(addr_type,NULL,BLE_HS_FOREVER,&p,gap_event,NULL);
}
static int gap_event(struct ble_gap_event *e,void *arg){
    (void)arg;
    if(e->type==BLE_GAP_EVENT_CONNECT){
        if(e->connect.status==0){connection=e->connect.conn_handle;audio_lab_connected(connection);
            struct ble_gap_upd_params params={.itvl_min=6,.itvl_max=12,.latency=0,.supervision_timeout=200};
            (void)ble_gap_update_params(connection,&params);rx_used=0;rx_discard=false;subscribed=false;epoch++;}
        else advertise();
    }else if(e->type==BLE_GAP_EVENT_DISCONNECT){
        audio_lab_disconnected();connection=BLE_HS_CONN_HANDLE_NONE;rx_used=0;rx_discard=false;subscribed=false;epoch++;advertise();
    }else if(e->type==BLE_GAP_EVENT_SUBSCRIBE && e->subscribe.attr_handle==changed_handle){
        subscribed=e->subscribe.cur_notify;
    }else if(e->type==BLE_GAP_EVENT_SUBSCRIBE && e->subscribe.attr_handle==audio_lab_tx_handle){
        audio_lab_subscribed(e->subscribe.cur_notify);
    }else if(e->type==BLE_GAP_EVENT_ADV_COMPLETE && connection==BLE_HS_CONN_HANDLE_NONE){advertise();}
    return 0;
}
static int access_gatt(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctxt,void *arg){
    (void)conn;(void)attr;(void)arg;
    if(ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR){
        xSemaphoreTake(view_lock,portMAX_DELAY);
        int rc=os_mbuf_append(ctxt->om,view,strlen(view));xSemaphoreGive(view_lock);
        return rc==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if(ctxt->op!=BLE_GATT_ACCESS_OP_WRITE_CHR)return BLE_ATT_ERR_UNLIKELY;
    uint8_t fragment[20];uint16_t length=0;
    if(OS_MBUF_PKTLEN(ctxt->om)>sizeof(fragment) || ble_hs_mbuf_to_flat(ctxt->om,fragment,sizeof(fragment),&length)!=0)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    for(unsigned i=0;i<length;i++){
        char c=(char)fragment[i];
        if(c=='\n'){
            if(!rx_discard && rx_used){
                event_t ev={.epoch=epoch,.button=-1};memcpy(ev.line,rx,rx_used);ev.line[rx_used]=0;
                rx_used=0;
                if(xQueueSend(events,&ev,0)!=pdTRUE)return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            rx_used=0;rx_discard=false;
        }else if(c==0 || rx_used>=sizeof(rx)-1){rx_discard=true;}
        else if(!rx_discard)rx[rx_used++]=c;
    }
    return 0;
}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,
     .characteristics=(struct ble_gatt_chr_def[]){
         {.uuid=&rx_uuid.u,.access_cb=access_gatt,.flags=BLE_GATT_CHR_F_WRITE},
         {.uuid=&tx_uuid.u,.access_cb=access_gatt,.flags=BLE_GATT_CHR_F_READ},
         {.uuid=&changed_uuid.u,.access_cb=access_gatt,.val_handle=&changed_handle,.flags=BLE_GATT_CHR_F_NOTIFY},
         {.uuid=&audio_control_uuid.u,.access_cb=audio_lab_access,.arg=(void *)0,.flags=BLE_GATT_CHR_F_WRITE},
         {.uuid=&audio_rx_uuid.u,.access_cb=audio_lab_access,.arg=(void *)1,.flags=BLE_GATT_CHR_F_WRITE_NO_RSP},
         {.uuid=&audio_tx_uuid.u,.access_cb=audio_lab_access,.arg=(void *)2,.val_handle=&audio_lab_tx_handle,.flags=BLE_GATT_CHR_F_NOTIFY},
         {.uuid=&audio_stats_uuid.u,.access_cb=audio_lab_access,.arg=(void *)3,.flags=BLE_GATT_CHR_F_READ},
         {0}}},{0}};
static void synced(void){
    int rc=ble_hs_util_ensure_addr(0);if(!rc)rc=ble_hs_id_infer_auto(0,&addr_type);
    if(!rc)rc=advertise();
    if(rc)ESP_LOGE(TAG,"BLE sync/advertise failed: %d",rc);
}
static void host_task(void *unused){(void)unused;nimble_port_run();vTaskDelete(NULL);}
static void button(bsp_btn_t b,bsp_btn_ev_t e,void *unused){
    (void)unused;
    if(e!=BSP_BTN_CLICK && !(b==BSP_BTN_DOWN&&e==BSP_BTN_LONG))return;
    event_t ev={.button=(b==BSP_BTN_DOWN&&e==BSP_BTN_LONG)?3:(int)b};
    (void)xQueueSend(events,&ev,0);
}
static bool store_key(bool erase){
    nvs_handle_t n;if(nvs_open("ble-sign-lab",NVS_READWRITE,&n)!=ESP_OK)return false;
    esp_err_t rc=erase?nvs_erase_key(n,"public"):nvs_set_blob(n,"public",state.key,32);
    if(erase&&rc==ESP_ERR_NVS_NOT_FOUND)rc=ESP_OK;
    if(rc==ESP_OK)rc=nvs_commit(n);
    nvs_close(n);return rc==ESP_OK;
}
static void load_key(void){
    nvs_handle_t n;if(nvs_open("ble-sign-lab",NVS_READONLY,&n)!=ESP_OK)return;
    size_t size=32;if(nvs_get_blob(n,"public",state.key,&size)==ESP_OK&&size==32)state.bound=true;
    nvs_close(n);
}
static void refresh(void){
    char key[65]="",candidate[65]="",next[WIRE_MAX];
    if(state.bound)lab_hex(state.key,32,key);
    if(state.candidate)lab_hex(state.proposed,32,candidate);
    snprintf(next,sizeof(next),"{\"v\":1,\"bound\":%s,\"key\":\"%s\",\"candidate\":\"%s\",\"pending\":%s,\"id\":%lu,\"message\":\"%s\",\"passed\":%lu,\"rejected\":%lu,\"timeouts\":%lu,\"rtt_ms\":%llu,\"note\":\"%s\"}",
        state.bound?"true":"false",key,candidate,state.pending?"true":"false",(unsigned long)state.id,
        state.pending?state.message:"",(unsigned long)state.passed,(unsigned long)state.rejected,
        (unsigned long)state.timed_out,(unsigned long long)state.elapsed_ms,note);
    xSemaphoreTake(view_lock,portMAX_DELAY);bool changed=strcmp(view,next)!=0;strcpy(view,next);xSemaphoreGive(view_lock);
    if(changed&&subscribed&&connection!=BLE_HS_CONN_HANDLE_NONE){
        uint8_t signal=1;struct os_mbuf *om=ble_hs_mbuf_from_flat(&signal,1);
        if(om)(void)ble_gatts_notify_custom(connection,changed_handle,om);
    }
    if(!bsp_lvgl_lock(100))return;
    lv_label_set_text(ui_state,note);
    lv_label_set_text_fmt(ui_key,"%s\n%.16s",state.candidate?"Confirm signer key":state.bound?"Bound signer":"Not bound",state.candidate?candidate:key);
    lv_label_set_text_fmt(ui_stats,"PASS %lu   REJECT %lu\nTIMEOUT %lu   RTT %llums\nBLE %s",(unsigned long)state.passed,(unsigned long)state.rejected,(unsigned long)state.timed_out,(unsigned long long)state.elapsed_ms,connection==BLE_HS_CONN_HANDLE_NONE?"disconnected":"connected");
    lv_label_set_text(ui_help,erase_confirm?"OK: erase binding\nUP: cancel":state.candidate?"OK: accept binding\nUP: reject":"OK: request signature\nHold DOWN: unbind");
    bsp_lvgl_unlock();
}
static void request(void){
    if(connection==BLE_HS_CONN_HANDLE_NONE){note="Connect Web signer first";return;}
    if(!state.bound){note="Bind a signer first";return;}
    if(state.pending){note="Waiting for signature";return;}
    uint8_t random[16];char hex[33],message[LAB_MESSAGE_MAX];esp_fill_random(random,sizeof(random));lab_hex(random,sizeof(random),hex);
    snprintf(message,sizeof(message),"RFLink BLE Lab v1|%lu|%s",(unsigned long)(state.id+1),hex);
    if(lab_request(&state,message,now_ms()))note="Waiting for signature";
}
static const char *string(cJSON *obj,const char *name){cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,name);return cJSON_IsString(v)?v->valuestring:NULL;}
static void command(const char *line){
    cJSON *obj=cJSON_Parse(line);if(!obj){note="Malformed JSON rejected";state.rejected++;return;}
    const char *op=string(obj,"op");uint8_t key[32],sig[64];
    if(op&&strcmp(op,"bind")==0){
        if(!lab_unhex(string(obj,"key"),key,32)||!lab_offer(&state,key)){note="Different signer rejected";state.rejected++;}
        else note=state.bound?"Signer already bound":"Press OK to bind signer";
    }else if(op&&strcmp(op,"request")==0){request();}
    else if(op&&strcmp(op,"signature")==0){
        cJSON *id=cJSON_GetObjectItemCaseSensitive(obj,"id");
        bool sane=cJSON_IsNumber(id)&&id->valuedouble>=1&&id->valuedouble<=UINT32_MAX&&id->valuedouble==(double)(uint32_t)id->valuedouble;
        bool valid=sane&&state.bound&&state.pending&&lab_unhex(string(obj,"sig"),sig,64)&&
            crypto_sign_verify_detached(sig,(const unsigned char *)state.message,strlen(state.message),state.key)==0;
        if(lab_result(&state,sane?(uint32_t)id->valuedouble:0,valid,now_ms())){
            note="Signature verified: PASS";ESP_LOGI(TAG,"PASS request=%lu rtt=%llums",(unsigned long)state.id,(unsigned long long)state.elapsed_ms);
        }else note="Signature/replay rejected";
    }else{state.rejected++;note="Unknown command rejected";}
    cJSON_Delete(obj);
}
static void create_ui(void){
    lv_obj_t *screen=lv_obj_create(NULL);lv_obj_set_style_bg_color(screen,lv_color_hex(0x101923),0);
    lv_obj_set_style_text_color(screen,lv_color_hex(0xe9f0f3),0);lv_obj_set_style_text_font(screen,&lv_font_montserrat_14,0);
    lv_obj_t *title=lv_label_create(screen);lv_label_set_text(title,"BLE LINK LAB");lv_obj_set_pos(title,18,22);
    ui_battery=lv_label_create(screen);lv_obj_set_pos(ui_battery,185,22);lv_label_set_text(ui_battery,"--%");
    ui_state=lv_label_create(screen);lv_obj_set_pos(ui_state,18,60);lv_obj_set_width(ui_state,204);
    ui_key=lv_label_create(screen);lv_obj_set_pos(ui_key,18,113);lv_obj_set_width(ui_key,204);
    ui_stats=lv_label_create(screen);lv_obj_set_pos(ui_stats,18,164);lv_obj_set_width(ui_stats,204);
    ui_help=lv_label_create(screen);lv_obj_set_pos(ui_help,18,247);lv_obj_set_width(ui_help,204);
    lv_screen_load(screen);
}
static bool crypto_selftest(void){
    uint8_t key[32],sig[64];
    return lab_unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",key,32)&&
        lab_unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",sig,64)&&
        crypto_sign_verify_detached(sig,(const unsigned char *)"",0,key)==0;
}
void app_main(void){
    ESP_ERROR_CHECK(nvs_flash_init()); /* Never erase user NVS on initialization error. */
    load_key();view_lock=xSemaphoreCreateMutex();events=xQueueCreate(8,sizeof(event_t));
    if(!view_lock||!events){ESP_LOGE(TAG,"Out of memory");return;}
    if(sodium_init()<0||!crypto_selftest()){ESP_LOGE(TAG,"Ed25519 self-test failed");return;}
    ESP_ERROR_CHECK(bsp_display_init());if(!bsp_lvgl_init()||!bsp_lvgl_lock(1000))return;
    create_ui();bsp_lvgl_unlock();bsp_display_backlight(70);
    ESP_ERROR_CHECK(bsp_button_init(button,NULL));bool battery=bsp_battery_init()==ESP_OK;
    ESP_ERROR_CHECK(audio_lab_init());
    ESP_ERROR_CHECK(nimble_port_init());
    ESP_ERROR_CHECK(ble_att_set_preferred_mtu(247));ble_svc_gap_init();ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set("Passport Sign Lab"));
    ESP_ERROR_CHECK(ble_gatts_count_cfg(services));ESP_ERROR_CHECK(ble_gatts_add_svcs(services));
    ble_hs_cfg.sync_cb=synced;
    if(xTaskCreate(host_task,"ble-host",6144,NULL,5,NULL)!=pdPASS){ESP_LOGE(TAG,"Host task allocation failed");return;}
    ESP_LOGI(TAG,"Ready; Ed25519 RFC8032 self-test PASS; bound=%d; no eFuse writes",state.bound);
    uint64_t battery_at=0;
    for(;;){
        event_t ev;bool got=xQueueReceive(events,&ev,pdMS_TO_TICKS(100))==pdTRUE;
        if(worker_epoch!=epoch){worker_epoch=epoch;lab_disconnect(&state);erase_confirm=false;note=connection==BLE_HS_CONN_HANDLE_NONE?"Disconnected; binding kept":"Web connected";}
        if(got){
            if(ev.button>=0){
                if(ev.button==BSP_BTN_UP){erase_confirm=false;state.candidate=false;note="Cancelled";}
                else if(ev.button==3){erase_confirm=true;note="Confirm unbind with OK";}
                else if(ev.button==BSP_BTN_OK){
                    if(erase_confirm){if(store_key(true)){lab_clear(&state);note="Binding erased";}else note="Storage error; binding kept";erase_confirm=false;}
                    else if(state.candidate){lab_model_t previous=state;if(lab_confirm(&state)&&store_key(false))note="Bound; press OK to test";else{state=previous;note="Binding save failed";}}
                    else request();
                }
            }else if(ev.epoch==worker_epoch && connection!=BLE_HS_CONN_HANDLE_NONE)command(ev.line);
        }
        if(lab_tick(&state,now_ms()))note="Signature timed out";
        refresh();
        if(now_ms()-battery_at>10000){battery_at=now_ms();ESP_LOGI(TAG,"Health heap=%lu main_stack_min=%u",(unsigned long)esp_get_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL));int soc=battery?bsp_battery_soc():-1;
            if(bsp_lvgl_lock(100)){if(soc<0)lv_label_set_text(ui_battery,"--%");else lv_label_set_text_fmt(ui_battery,"%d%%",soc);bsp_lvgl_unlock();}}
    }
}
