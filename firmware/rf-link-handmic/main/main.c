#include "hm_core.h"
#include "hm_ble.h"
#include "hm_audio.h"
#include "hm_ui.h"
#include "hm_product.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>

static hm_core_t core;
static hm_product_t product;
static SemaphoreHandle_t guard;
static QueueHandle_t events;
static _Atomic uint32_t generation,physical_epoch;
static _Atomic bool physical_keys[3],audio_permitted;
static uint32_t accepted_epoch;
static portMUX_TYPE input_mux=portMUX_INITIALIZER_UNLOCKED;
static nvs_handle_t storage;
static hm_pair_code_t pair_code;
static uint64_t pair_deadline,message_until;
static hm_product_actions_t removal_pending;
static char message[128],device_name[64];
static uint32_t ui_max_us;
static const char *TAG="rf-link";
typedef enum { EV_LINK,EV_CONTROL,EV_BUTTON,EV_PASSKEY,EV_FRAGMENT } kind_t;
typedef struct {kind_t kind;uint32_t gen,value;uint16_t length;int button,event;char json[513];} event_t;
static uint64_t now_ms(void){return esp_timer_get_time()/1000;}
static bool raw_volume(uint8_t v,void *ctx){
    (void)ctx;if(nvs_set_u8(storage,"volume",v)==ESP_OK&&nvs_commit(storage)==ESP_OK)return true;
    (void)nvs_set_u8(storage,"volume",product.saved_volume);(void)nvs_commit(storage);return false;
}
static bool save_theme(const char *id,void *ctx){
    (void)ctx;if(nvs_set_str(storage,"theme",id)==ESP_OK&&nvs_commit(storage)==ESP_OK)return true;
    (void)nvs_set_str(storage,"theme",hm_product_palette(product.saved_theme)->id);(void)nvs_commit(storage);return false;
}
static bool save_spectrum(bool enabled,void *ctx){
    (void)ctx;
    if(nvs_set_u8(storage,"spectrum",enabled?1:0)==ESP_OK&&nvs_commit(storage)==ESP_OK)return true;
    (void)nvs_set_u8(storage,"spectrum",product.spectrum_enabled?1:0);(void)nvs_commit(storage);return false;
}
static bool remote_volume(uint8_t v,void *ctx){
    (void)ctx;if(hm_product_volume_busy(&product)||!raw_volume(v,NULL))return false;
    hm_product_remote_volume(&product,v);return true;
}
static void post(const event_t *e){if(events)(void)xQueueSend(events,e,0);}
static void link_changed(uint32_t gen,bool connected,bool secure,uint16_t maximum){
    (void)connected;(void)secure;(void)maximum;generation=gen;event_t e={.kind=EV_LINK,.gen=gen};post(&e);
}
static void control_received(uint32_t gen,const uint8_t *json,size_t len){
    if(len>512)return;
    event_t e={.kind=EV_CONTROL,.gen=gen,.length=len};memcpy(e.json,json,len);post(&e);
}
static void passkey_received(uint32_t gen,uint32_t key){event_t e={.kind=EV_PASSKEY,.gen=gen,.value=key};post(&e);}
static void fragment_error(uint32_t gen){event_t e={.kind=EV_FRAGMENT,.gen=gen};post(&e);}
static void button_received(bsp_btn_t b,bsp_btn_ev_t ev,void *arg){
    (void)arg;
    if((unsigned)b>=3)return;
    if(ev==BSP_BTN_PRESS||ev==BSP_BTN_RELEASE){
        portENTER_CRITICAL(&input_mux);
        physical_keys[b]=ev==BSP_BTN_PRESS;
        if(b==BSP_BTN_OK&&ev==BSP_BTN_RELEASE){audio_permitted=false;physical_epoch++;}
        portEXIT_CRITICAL(&input_mux);
        if(b==BSP_BTN_OK&&ev==BSP_BTN_RELEASE)hm_ble_revoke_audio();
    }
    /* BSP click/double are delayed and omit 500..799ms presses. Product
     * short actions are synthesized exactly once from RELEASE instead. */
    if(ev!=BSP_BTN_PRESS&&ev!=BSP_BTN_RELEASE&&ev!=BSP_BTN_LONG)return;
    event_t e={.kind=EV_BUTTON,.button=b,.event=ev,.value=physical_epoch};post(&e);
}
static void set_message(const char *s,uint64_t now){snprintf(message,sizeof message,"%s",s);message_until=now+2000;}
static void send_control(const char *json,bool critical,bool hello){
    if(!json[0]||!hm_ble_authenticated())return;
    if(!hm_ble_send_control_priority(generation,json,strlen(json),hello||!core.s.sid?20:core.value_max,critical?HM_BLE_CRITICAL:HM_BLE_RESPONSE)){
        audio_permitted=false;char ignored[513];hm_core_stop(&core,"control_backpressure",ignored);hm_ble_clear_audio(generation);
        set_message("暂时无法讲话，请松开重试",now_ms());
    }
}
static void local_stop(const char *reason){audio_permitted=false;char reply[513]={0};hm_core_stop(&core,reason,reply);hm_ble_clear_audio(generation);send_control(reply,true,false);}
static void observe_product(uint64_t now){if(!hm_ble_ready()&&!product.observed)return;hm_product_pair_code_sync(&pair_code,hm_ble_generation(),hm_ble_pairing_open(),hm_ble_authenticated());hm_product_observe(&product,&core.s,hm_ble_bonded(),hm_ble_authenticated(),hm_ble_pairing_open(),now);}
static void apply_actions(hm_product_actions_t actions,uint64_t now){
    char reply[513]={0};
    if(actions&HM_ACTION_PTT_UP){audio_permitted=false;hm_core_press(&core,false,now,reply);hm_ble_clear_audio(generation);send_control(reply,true,false);}
    if(actions&HM_ACTION_PTT_DOWN){
        if(physical_keys[BSP_BTN_OK]&&accepted_epoch==physical_epoch&&product.page==HM_PAGE_MAIN&&!product.ok_blocked){
            hm_core_press(&core,true,now,reply);send_control(reply,true,false);
        }
    }
    if(actions&HM_ACTION_PAIR_CLOSE){hm_ble_pairing(false);pair_deadline=0;pair_code.valid=false;}
    if(actions&HM_ACTION_PAIR_OPEN){
        local_stop("pairing");pair_code.valid=false;pair_deadline=now+60000;hm_ble_pairing(true);
        hm_product_action_result(&product,HM_ACTION_PAIR_OPEN,hm_ble_pairing_open(),now);
    }
    if(actions&(HM_ACTION_UNPAIR|HM_ACTION_UNPAIR_AND_PAIR)){
        local_stop("unpair");removal_pending=actions&(HM_ACTION_UNPAIR|HM_ACTION_UNPAIR_AND_PAIR);
        pair_code.valid=false;pair_deadline=0;hm_ble_unpair();
    }
}
static void sync_volume(void){
    if(core.s.volume!=product.volume){char reply[513]={0};hm_core_preview_volume(&core,product.volume,reply);send_control(reply,false,false);}
    core.remote_volume_busy=hm_product_volume_busy(&product);
}
static void button_event(const event_t *e,uint64_t now){
    hm_product_key_t key=(hm_product_key_t)e->button;
    if((unsigned)key>=3)return;
    if(e->event==BSP_BTN_PRESS&&key==HM_KEY_OK&&product.page==HM_PAGE_MAIN&&
       (e->value!=physical_epoch||!physical_keys[BSP_BTN_OK]))return;
    if(removal_pending&&e->event!=BSP_BTN_RELEASE)return;
    hm_product_actions_t actions=0;
    if(e->event==BSP_BTN_PRESS)actions=hm_product_button(&product,key,HM_KEY_PRESS,now);
    else if(e->event==BSP_BTN_LONG)actions=hm_product_button(&product,key,HM_KEY_LONG,now);
    else if(e->event==BSP_BTN_RELEASE){
        actions=hm_product_release_click(&product,key,now);
    }
    if(actions&HM_ACTION_PTT_DOWN)accepted_epoch=e->value;
    apply_actions(actions,now);sync_volume();
}
static void received_audio(const hm_snapshot_t *s,uint64_t now,void *ctx){(void)ctx;hm_product_received_audio(&product,s,now);}
static void format_frequency(char *out,size_t n,uint32_t hz){snprintf(out,n,"%lu.%03lu MHz",(unsigned long)(hz/1000000),(unsigned long)((hz%1000000)/1000));}
static void refresh_ui(void){
    static hm_ui_snapshot_t ui;
    uint8_t levels[16],peaks[16];uint64_t start=esp_timer_get_time();hm_audio_spectrum(levels,peaks);
    memset(&ui,0,sizeof ui);xSemaphoreTake(guard,portMAX_DELAY);uint64_t now=now_ms();
    hm_snapshot_t s;hm_core_get_snapshot(&core,now,&s);observe_product(now);ui.p=product;
    uint64_t stream_started=core.stream_started_ms;
    ui.now_ms=now;ui.passkey=pair_code.value;ui.passkey_valid=pair_code.valid;ui.audio_fault=!hm_audio_ok();
    xSemaphoreGive(guard); /* Format strings and layout only after releasing the audio state lock. */
    if(hm_ble_pairing_open()&&pair_deadline>now)ui.pair_seconds=(pair_deadline-now+999)/1000;
    if(s.direction==HM_DIRECTION_MIC)ui.tx_elapsed_s=(now-stream_started)/1000;
    for(unsigned i=0;i<16;i++){ui.spectrum[i]=(levels[i]*100u+127)/255;ui.peaks[i]=(peaks[i]*100u+127)/255;}
    if(s.position.valid){
        snprintf(ui.position,sizeof ui.position,"%.5f, %.5f",(double)s.position.lat_e7/1e7,(double)s.position.lon_e7/1e7);
        if(s.position.distance_known){
            if(s.position.distance_m>=1000)snprintf(ui.distance,sizeof ui.distance,"%s%.1f km",s.position.approx?"约 ":"",s.position.distance_m/1000.0);
            else snprintf(ui.distance,sizeof ui.distance,"%s%lu m",s.position.approx?"约 ":"",(unsigned long)s.position.distance_m);
        }
        snprintf(ui.age,sizeof ui.age,"位置 %lu 秒前",(unsigned long)s.position.age_s);
    }
    if(s.tele.valid){char a[24],b[24];format_frequency(a,sizeof a,s.tele.frequency_a_hz);format_frequency(b,sizeof b,s.tele.frequency_b_hz);snprintf(ui.frequency,sizeof ui.frequency,"A %s\nB %s",a,b);}
    else if(s.rig.valid)format_frequency(ui.frequency,sizeof ui.frequency,s.rig.frequency_hz);
    if(s.rig.valid){
        snprintf(ui.grid,sizeof ui.grid,"%s",s.rig.grid);snprintf(ui.rig,sizeof ui.rig,"%s",s.rig.model);snprintf(ui.antenna,sizeof ui.antenna,"%s",s.rig.antenna);snprintf(ui.height,sizeof ui.height,"%lu m",(unsigned long)s.rig.height_m);
        if(!s.position.valid)snprintf(ui.age,sizeof ui.age,"资料 %lu 秒前",(unsigned long)s.rig.age_s);
    }
    if(now<message_until)snprintf(ui.message,sizeof ui.message,"%s",message);
    hm_ui_set_phase(&ui);
    snprintf(ui.device_name,sizeof ui.device_name,"%s",device_name);
    snprintf(ui.firmware_version,sizeof ui.firmware_version,"%s",esp_app_get_description()->version);
    if(bsp_lvgl_lock(20)){hm_ui_render(&ui);bsp_lvgl_unlock();}
    uint32_t elapsed=(uint32_t)(esp_timer_get_time()-start);if(elapsed>ui_max_us)ui_max_us=elapsed;
}
static esp_err_t load_identity(char id[37],uint8_t *volume,char theme[24],bool *spectrum){
    esp_err_t rc=nvs_open("rf-handmic",NVS_READWRITE,&storage);if(rc!=ESP_OK)return rc;
    size_t n=37;rc=nvs_get_str(storage,"device_id",id,&n);
    if(rc==ESP_ERR_NVS_NOT_FOUND){uint8_t b[16];esp_fill_random(b,sizeof b);b[6]=(b[6]&15)|0x40;b[8]=(b[8]&63)|0x80;
        snprintf(id,37,"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
        rc=nvs_set_str(storage,"device_id",id);if(rc==ESP_OK)rc=nvs_commit(storage);
    }
    if(rc!=ESP_OK)return rc;
    if(strlen(id)!=36)return ESP_ERR_INVALID_STATE;
    rc=nvs_get_u8(storage,"volume",volume);if(rc==ESP_ERR_NVS_NOT_FOUND){*volume=90;rc=ESP_OK;}if(rc!=ESP_OK||*volume>100)return ESP_ERR_INVALID_STATE;
    n=24;rc=nvs_get_str(storage,"theme",theme,&n);if(rc!=ESP_OK)snprintf(theme,24,"phosphor_green");
    uint8_t flag=0;rc=nvs_get_u8(storage,"spectrum",&flag);
    if(rc==ESP_ERR_NVS_NOT_FOUND){*spectrum=false;return ESP_OK;}
    if(rc!=ESP_OK)return rc;
    *spectrum=flag==1;return ESP_OK;
}
void app_main(void){
    char id[37],theme[24];uint8_t volume=90;bool spectrum=false;
    esp_err_t settings=nvs_flash_init();
    if(settings==ESP_OK)settings=load_identity(id,&volume,theme,&spectrum);
    if(settings!=ESP_OK){
        /* Corrupt/unavailable storage is never repaired by erasing user data. */
        ESP_LOGE(TAG,"Settings unavailable: %s",esp_err_to_name(settings));
        if(bsp_display_init()==ESP_OK&&bsp_lvgl_init()&&bsp_lvgl_lock(1000)){
            static hm_ui_snapshot_t fault;fault.settings_fault=true;fault.p.theme=HM_THEME_GREEN;fault.p.core.battery=-1;
            hm_ui_create();hm_ui_render(&fault);bsp_lvgl_unlock();bsp_display_backlight(65);
        }
        for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
    }
    snprintf(device_name,sizeof device_name,"RF Link · %.4s",id+32);
    guard=xSemaphoreCreateMutex();events=xQueueCreate(12,sizeof(event_t));if(!guard||!events){ESP_LOGE(TAG,"Control allocation failed");return;}
    hm_core_init(&core,id,volume);core.sid_counter=esp_random()&0x7fffffff;
    hm_product_init(&product,volume,theme,(hm_product_store_t){.store_volume=raw_volume,.store_theme=save_theme,.store_spectrum=save_spectrum});
    hm_product_load_spectrum(&product,spectrum);
    hm_core_set_volume_store(&core,remote_volume,NULL);
    ESP_ERROR_CHECK(bsp_display_init());if(!bsp_lvgl_init()||!bsp_lvgl_lock(1000))return;hm_ui_create();bsp_lvgl_unlock();bsp_display_backlight(65);
    bool battery=bsp_battery_init()==ESP_OK;
    esp_err_t audio=hm_audio_init(&core,guard,&generation,&audio_permitted);if(audio!=ESP_OK)ESP_LOGE(TAG,"Audio unavailable: %s",esp_err_to_name(audio));
    core.audio_report=hm_audio_stats_json;hm_audio_set_spectrum_enabled(spectrum);
    hm_audio_set_history(received_audio,NULL);
    ESP_ERROR_CHECK(bsp_button_init(button_received,NULL));
    const hm_ble_callbacks_t callbacks={.link=link_changed,.control=control_received,.audio=hm_audio_receive,.flow=hm_audio_flow,.passkey=passkey_received,.fragment_error=fragment_error};
    ESP_ERROR_CHECK(hm_ble_init(id,&callbacks));hm_ble_set_audio_gate(&audio_permitted);char json[513];hm_core_info(&core,json);hm_ble_set_info(json);
    uint32_t active_generation=UINT32_MAX,last_stream=0,last_sid=0,last_credit=0,last_press=0;bool active_secure=false,last_down=false;
    uint64_t flow_at=0,ui_at=0,status_at=0,battery_at=0,health_at=0;bool applied_spectrum=spectrum;
    ESP_LOGI(TAG,"Ready RF Link UIv3 theme=%s volume=%u spectrum=%u",hm_product_palette(product.theme)->id,volume,(unsigned)spectrum);
    for(;;){
        event_t event;bool got=xQueueReceive(events,&event,pdMS_TO_TICKS(10))==pdTRUE;
        xSemaphoreTake(guard,portMAX_DELAY);uint64_t now=now_ms();
        uint32_t gen=hm_ble_generation();bool secure=hm_ble_authenticated();
        if(gen!=active_generation||secure!=active_secure){
            pair_code.valid=false;
            audio_permitted=false;hm_ble_clear_audio(active_generation);hm_core_close(&core);generation=gen;active_generation=gen;active_secure=secure;
            if(secure){hm_core_open(&core,hm_ble_max_value(),now);if(physical_keys[BSP_BTN_OK]){char ignored[513];hm_core_press(&core,true,now,ignored);}pair_code.valid=false;}
        }
        core.gatt_max=hm_ble_max_value();observe_product(now);
        if(core.s.physical_ptt&&(!physical_keys[BSP_BTN_OK]||accepted_epoch!=physical_epoch)){
            char reply[513]={0};audio_permitted=false;hm_core_press(&core,false,now,reply);hm_ble_clear_audio(gen);send_control(reply,true,false);
        }
        if(removal_pending){
            int result=hm_ble_unpair_result();
            if(result!=0){hm_product_actions_t a=removal_pending;removal_pending=0;hm_product_action_result(&product,a,result==1,now);
                if(result==1&&(a&HM_ACTION_UNPAIR_AND_PAIR))apply_actions(HM_ACTION_PAIR_OPEN,now);
            }
        }
        if(got){
            if(event.kind==EV_BUTTON)button_event(&event,now);
            else if(event.kind==EV_PASSKEY)hm_product_pair_code_receive(&pair_code,event.gen,gen,event.value,hm_ble_pairing_open(),secure,product.page==HM_PAGE_PAIR);
            else if(event.kind==EV_CONTROL&&event.gen==gen&&secure){
                hm_ble_get_audio_stats(&core.transport_boot_submitted,&core.transport_boot_dropped);core.remote_volume_busy=hm_product_volume_busy(&product);
                char reply[513]={0};cJSON *j=cJSON_Parse(event.json);cJSON *op=j?cJSON_GetObjectItemCaseSensitive(j,"op"):NULL;
                bool hello=cJSON_IsString(op)&&!strcmp(op->valuestring,"hello");cJSON_Delete(j);
                if(hm_core_command(&core,event.json,event.length,now,reply))send_control(reply,false,hello);
            }else if(event.kind==EV_FRAGMENT&&event.gen==gen)ESP_LOGW(TAG,"Malformed control fragment");
        }
        now=now_ms();
        /* Lost RELEASE must stop core and clear held-key repeat state; no synthetic
         * short action for an event that was never delivered. */
        for(unsigned k=0;k<3;k++)if(uxQueueMessagesWaiting(events)==0&&product.key_down[k]&&!physical_keys[k])apply_actions(hm_product_button(&product,(hm_product_key_t)k,HM_KEY_RELEASE,now),now);
        if(!physical_keys[BSP_BTN_OK]&&core.s.physical_ptt){char reply[513]={0};hm_core_press(&core,false,now,reply);hm_ble_clear_audio(gen);send_control(reply,true,false);}
        observe_product(now);
        bool release_queued=false;
        for(unsigned k=0;k<3;k++)if(product.key_down[k]&&!physical_keys[k])release_queued=true;
        if(!release_queued)apply_actions(hm_product_tick(&product,now),now);
        sync_volume();
        now=now_ms();for(unsigned n=0;n<4;n++){char reply[513]={0};if(!hm_core_tick(&core,now,reply))break;send_control(reply,true,false);}
        if(core.s.sid!=last_sid||core.s.stream_id!=last_stream){
            audio_permitted=false;hm_ble_clear_audio(gen);
            portENTER_CRITICAL(&input_mux);
            if(core.s.direction==HM_DIRECTION_MIC&&physical_keys[BSP_BTN_OK]&&accepted_epoch==physical_epoch&&product.page==HM_PAGE_MAIN)audio_permitted=true;
            portEXIT_CRITICAL(&input_mux);
            last_sid=core.s.sid;last_stream=core.s.stream_id;flow_at=0;
        }
        if(core.s.direction!=HM_DIRECTION_MIC)audio_permitted=false;
        uint64_t period=(core.s.direction!=HM_DIRECTION_NONE||core.s.pending_ptt)?100:500;
        if(core.s.sid&&(now-flow_at>=period||core.s.next_seq-last_credit>=40/hm_core_frame_ms(&core)||core.s.press_id!=last_press||core.s.physical_ptt!=last_down)){
            uint8_t flow[24];if(hm_core_make_flow(&core,flow)&&hm_ble_notify_flow(gen,flow,sizeof flow)){flow_at=now;last_credit=core.s.next_seq;last_press=core.s.press_id;last_down=core.s.physical_ptt;}
        }
        if(now-status_at>=100){status_at=now;hm_core_status(&core,json);hm_ble_set_status(json);}
        bool spectrum_setting=product.spectrum_enabled;
        xSemaphoreGive(guard);
        if(spectrum_setting!=applied_spectrum){hm_audio_set_spectrum_enabled(spectrum_setting);applied_spectrum=spectrum_setting;}
        hm_ble_poll(now);
        if(now-battery_at>=10000){battery_at=now;int soc=battery?bsp_battery_soc():-1;xSemaphoreTake(guard,portMAX_DELAY);char reply[513]={0};hm_core_set_battery(&core,soc,reply);send_control(reply,false,false);xSemaphoreGive(guard);}
        if(now-ui_at>=50){ui_at=now;refresh_ui();}
        if(now-health_at>=10000){health_at=now;ESP_LOGI(TAG,"Health heap=%lu stack=%u ui_max_us=%lu",(unsigned long)esp_get_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL),(unsigned long)ui_max_us);ui_max_us=0;}
    }
}
