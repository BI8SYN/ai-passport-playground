#include "ear_controller.h"
#include "ear_synth.h"
#include "ear_ui.h"
#include "bsp_audio.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <math.h>

typedef struct {int type,key,value;bool down;uint32_t generation;int64_t at;} input_t;
typedef struct {et_score_t score;uint32_t generation;} play_t;
typedef struct {uint32_t version;et_config_t config;et_result_t history[ET_MODE_COUNT*3];} saved_t;
static et_app_t app;
static QueueHandle_t input_queue,play_queue,save_queue,volume_queue;
static uint32_t generation;
static const char *TAG="ear-trainer";
static uint32_t dropped;

static void on_button(bsp_btn_t button,bsp_btn_ev_t event,void *user){
    (void)user;if(event!=BSP_BTN_PRESS&&event!=BSP_BTN_RELEASE)return;
    input_t e={.type=0,.key=(int)button,.down=event==BSP_BTN_PRESS,.at=esp_timer_get_time()};
    if(xQueueSend(input_queue,&e,0)!=pdTRUE)dropped++;
}
static void storage(void *unused){
    (void)unused;static saved_t data;
    while(xQueueReceive(save_queue,&data,portMAX_DELAY)==pdTRUE){nvs_handle_t h;if(nvs_open("ear-lab",NVS_READWRITE,&h)!=ESP_OK)continue;
        esp_err_t err=nvs_set_blob(h,"state",&data,sizeof(data));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);
        if(err!=ESP_OK)ESP_LOGW(TAG,"Save failed: %s",esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}
static bool sane(const et_config_t *c){return c->mode<ET_MODE_COUNT&&c->level<3&&c->variant<3&&c->speed<3&&c->volume<=100&&(c->questions==0||c->questions==5||c->questions==10||c->questions==20)&&c->offset_ms>=-150&&c->offset_ms<=150;}
static void load(void){
    esp_err_t err=nvs_flash_init();if(err!=ESP_OK){ESP_LOGW(TAG,"NVS unavailable, session-only: %s",esp_err_to_name(err));return;}
    static saved_t data;nvs_handle_t h;size_t size=sizeof(data);
    if(nvs_open("ear-lab",NVS_READONLY,&h)!=ESP_OK)return;
    if(nvs_get_blob(h,"state",&data,&size)==ESP_OK&&size==sizeof(data)&&(data.version>=1&&data.version<=ET_SETTINGS_VERSION)&&sane(&data.config)){
        app.config=data.config;memcpy(app.history,data.history,sizeof(app.history));
        if(et_upgrade_config(&app.config,data.version))app.save=true;
    }
    nvs_close(h);
}
static void audio(void *unused){
    (void)unused;static et_synth_t synth;static play_t request;int16_t pcm[160],previous=0;bool muted=false;
    esp_err_t err=bsp_audio_init();if(err==ESP_OK)err=bsp_audio_set_format(ET_RATE,16,1);
    input_t status={.type=1,.value=err==ESP_OK};xQueueSend(input_queue,&status,portMAX_DELAY);
    if(err!=ESP_OK){ESP_LOGE(TAG,"Audio failed: %s",esp_err_to_name(err));vTaskDelete(NULL);return;}
    bsp_audio_set_volume(et_codec_level(et_defaults().volume));et_synth_init(&synth,NULL);
    /* Keep DMA fed with silence too. Six 240-frame descriptors at 16kHz = 90ms.
       onset is an estimate, adjustable in rhythm settings; not lab calibration. */
    for(;;){
        uint8_t level;
        if(xQueueReceive(volume_queue,&level,0)==pdTRUE){muted=level==0;bsp_audio_set_volume(et_codec_level(level));}
        if(xQueueReceive(play_queue,&request,0)==pdTRUE){
            /* Fade the previous tail before replacing a sequence. */
            for(int i=0;i<160;i++)pcm[i]=muted?0:(int16_t)((int32_t)previous*(159-i)/160);
            err=bsp_audio_write(pcm,sizeof(pcm));if(err!=ESP_OK)break;
            et_synth_init(&synth,&request.score);
            if(request.score.count){input_t e={.type=2,.generation=request.generation,.at=esp_timer_get_time()+90000};xQueueSend(input_queue,&e,portMAX_DELAY);}
        }
        et_synth_output(&synth,pcm,160,muted);previous=pcm[159];
        err=bsp_audio_write(pcm,sizeof(pcm));if(err!=ESP_OK)break;
    }
    ESP_LOGE(TAG,"Audio stream failed: %s",esp_err_to_name(err));status.value=0;xQueueSend(input_queue,&status,portMAX_DELAY);vTaskDelete(NULL);
}
static void poll(lv_timer_t *timer){
    (void)timer;input_t e;int64_t now=esp_timer_get_time();
    for(int n=0;n<32&&xQueueReceive(input_queue,&e,0)==pdTRUE;n++){
        if(e.type==0)et_app_key(&app,e.key,e.down,e.at);
        else if(e.type==1){app.audio_ok=e.value;app.dirty=true;if(!e.value&&app.page==ET_PRACTICE){app.page=ET_SETUP;app.focus=0;app.generation++;}}
        else if(e.type==2)et_app_audio_start(&app,e.generation,e.at);
        else if(e.type==3){app.battery=e.value;app.dirty=true;}
    }
    et_app_tick(&app,now);
    if(app.generation!=generation||app.play){
        static play_t request;memset(&request,0,sizeof(request));request.generation=app.generation;
        if(app.play){
            request.score=app.question.score;
            if(app.arpeggio && !et_arpeggiate(&app.question.score,&request.score))request.score=app.question.score;
            app.play=false;
        }
        xQueueOverwrite(play_queue,&request);generation=app.generation;
    }
    if(app.volume_changed){uint8_t level=app.config.volume;xQueueOverwrite(volume_queue,&level);app.volume_changed=false;}
    if(app.save){static saved_t data;data.version=ET_SETTINGS_VERSION;data.config=app.config;memcpy(data.history,app.history,sizeof(data.history));xQueueOverwrite(save_queue,&data);app.save=false;}
    static int phase=-1;int next_phase=app.rhythm_at&&now>=app.rhythm_at;
    if(app.page==ET_PRACTICE&&app.config.mode==ET_RHYTHM&&phase!=next_phase){app.dirty=true;phase=next_phase;}
    if(app.dirty){et_ui_refresh(&app,now);app.dirty=false;}
}
void app_main(void){
    et_app_init(&app);app.seed=esp_random();load();app.volume_changed=true;
    ESP_LOGI(TAG,"Settings: volume=%u, storage=v%u",(unsigned)app.config.volume,(unsigned)ET_SETTINGS_VERSION);
    input_queue=xQueueCreate(32,sizeof(input_t));play_queue=xQueueCreate(1,sizeof(play_t));save_queue=xQueueCreate(1,sizeof(saved_t));volume_queue=xQueueCreate(1,sizeof(uint8_t));
    if(!input_queue||!play_queue||!save_queue||!volume_queue){ESP_LOGE(TAG,"Queue allocation failed");return;}
    if(bsp_display_init()!=ESP_OK||!bsp_lvgl_init()){ESP_LOGE(TAG,"Display initialization failed");return;}
    if(!bsp_lvgl_lock(1000))return;
    et_ui_create();et_ui_refresh(&app,esp_timer_get_time());lv_timer_create(poll,5,NULL);bsp_lvgl_unlock();bsp_display_backlight(80);
    if(xTaskCreate(audio,"ear-audio",6144,NULL,6,NULL)!=pdPASS)ESP_LOGE(TAG,"Audio task allocation failed");
    if(xTaskCreate(storage,"ear-save",3072,NULL,2,NULL)!=pdPASS)ESP_LOGE(TAG,"Storage task allocation failed");
    ESP_ERROR_CHECK(bsp_button_init(on_button,NULL));bool battery=bsp_battery_init()==ESP_OK;
    ESP_LOGI(TAG,"Ready: offline EAR LAB, five training modes; raw PRESS rhythm timestamps");
    for(;;){input_t e={.type=3,.value=battery?bsp_battery_soc():-1};xQueueSend(input_queue,&e,0);
        ESP_LOGI(TAG,"Health: heap=%lu, dropped input=%lu",(unsigned long)esp_get_free_heap_size(),(unsigned long)dropped);vTaskDelay(pdMS_TO_TICKS(15000));}
}
