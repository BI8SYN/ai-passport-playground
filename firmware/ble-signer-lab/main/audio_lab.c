/* Disposable PCM link probe. No persistent audio, network, or credential changes. */
#include "audio_lab.h"
#include "bsp_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SAMPLES 80
#define HEADER 8
#define CAPACITY 244
#define MAGIC 0xa001
uint16_t audio_lab_tx_handle;
typedef struct {uint16_t length;uint8_t data[CAPACITY];} packet_t;
static QueueHandle_t packets;
static _Atomic uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static _Atomic unsigned mode, generation;
static _Atomic bool subscribed;
static _Atomic uint32_t rx_bytes,tx_bytes,rx_packets,tx_packets,overflow,notify_fail,underrun,io_errors,bad_packets,rx_gaps;
static _Atomic uint32_t mic_rms,play_rms;
static uint32_t previous_rx;
static bool have_rx;
static uint16_t u16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return u16(p)|((uint32_t)u16(p+2)<<16);}
static void put16(uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static void put32(uint8_t *p,uint32_t v){put16(p,v);put16(p+2,v>>16);}
static uint32_t rms(const int16_t *p,size_t count){double sum=0;for(size_t i=0;i<count;i++)sum+=(double)p[i]*p[i];return (uint32_t)sqrt(sum/count);}
static void stop(void){mode=0;generation++;if(packets)xQueueReset(packets);}
void audio_lab_connected(uint16_t conn){stop();connection=conn;subscribed=false;}
void audio_lab_disconnected(void){stop();connection=BLE_HS_CONN_HANDLE_NONE;subscribed=false;}
void audio_lab_subscribed(bool value){subscribed=value;if(!value)stop();}
static void transmit(const uint8_t *data,size_t length){
    uint16_t conn=connection;
    if(conn==BLE_HS_CONN_HANDLE_NONE||!subscribed)return;
    struct os_mbuf *om=ble_hs_mbuf_from_flat(data,length);
    if(!om){notify_fail++;return;}
    int rc=ble_gatts_notify_custom(conn,audio_lab_tx_handle,om);
    if(rc)notify_fail++;else{tx_bytes+=length-HEADER;tx_packets++;}
}
int audio_lab_access(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctxt,void *arg){
    (void)attr;unsigned kind=(unsigned)(uintptr_t)arg;
    if(kind==3&&ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR){
        char json[512];
        int n=snprintf(json,sizeof(json),"{\"v\":1,\"mode\":%u,\"rate\":8000,\"mtu\":%u,\"rx_bytes\":%lu,\"tx_bytes\":%lu,\"rx_packets\":%lu,\"tx_packets\":%lu,\"overflow\":%lu,\"notify_fail\":%lu,\"underrun\":%lu,\"io_errors\":%lu,\"bad_packets\":%lu,\"rx_gaps\":%lu,\"mic_rms\":%lu,\"play_rms\":%lu,\"queued\":%u,\"heap\":%lu}",mode,ble_att_mtu(conn),(unsigned long)rx_bytes,(unsigned long)tx_bytes,(unsigned long)rx_packets,(unsigned long)tx_packets,(unsigned long)overflow,(unsigned long)notify_fail,(unsigned long)underrun,(unsigned long)io_errors,(unsigned long)bad_packets,(unsigned long)rx_gaps,(unsigned long)mic_rms,(unsigned long)play_rms,(unsigned)uxQueueMessagesWaiting(packets),(unsigned long)esp_get_free_heap_size());
        return os_mbuf_append(ctxt->om,json,n)==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if(ctxt->op!=BLE_GATT_ACCESS_OP_WRITE_CHR)return BLE_ATT_ERR_UNLIKELY;
    packet_t packet={0};
    if(OS_MBUF_PKTLEN(ctxt->om)>CAPACITY||ble_hs_mbuf_to_flat(ctxt->om,packet.data,CAPACITY,&packet.length))return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if(kind==0){
        if(packet.length!=1||packet.data[0]>3)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        stop();
        if(!packet.data[0])return 0;
        rx_bytes=tx_bytes=rx_packets=tx_packets=overflow=notify_fail=underrun=io_errors=bad_packets=rx_gaps=0;
        mic_rms=play_rms=0;have_rx=false;
        if(packet.data[0]&&(!subscribed||ble_att_mtu(conn)<171))return BLE_ATT_ERR_UNLIKELY;
        mode=packet.data[0];return 0;
    }
    if(kind!=1||(mode!=2&&mode!=3))return BLE_ATT_ERR_UNLIKELY;
    if(packet.length<HEADER+2||u16(packet.data+6)!=MAGIC||u16(packet.data+4)*2+HEADER!=packet.length){bad_packets++;return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;}
    uint32_t seq=u32(packet.data);if(have_rx&&seq!=previous_rx+1)rx_gaps++;previous_rx=seq;have_rx=true;
    rx_bytes+=packet.length-HEADER;rx_packets++;
    if(xQueueSend(packets,&packet,0)!=pdTRUE)overflow++;
    return 0;
}
static void input_task(void *arg){
    (void)arg;int16_t pcm[SAMPLES];uint8_t wire[SAMPLES*2+HEADER];uint32_t seq=0;unsigned seen=0;
    for(;;){
        if(bsp_audio_read(pcm,sizeof(pcm))!=ESP_OK){io_errors++;vTaskDelay(pdMS_TO_TICKS(10));continue;}
        if(seen!=generation){seen=generation;seq=0;}
        if(mode!=1)continue;
        mic_rms=rms(pcm,SAMPLES);put32(wire,seq++);put16(wire+4,SAMPLES);put16(wire+6,MAGIC);memcpy(wire+HEADER,pcm,sizeof(pcm));
        transmit(wire,sizeof(wire));
    }
}
static void output_task(void *arg){
    (void)arg;packet_t packet;int16_t silence[SAMPLES]={0};bool primed=false;unsigned seen=0;
    for(;;){
        if(seen!=generation){seen=generation;primed=false;}
        unsigned current=mode;
        if(current==3){
            if(xQueueReceive(packets,&packet,pdMS_TO_TICKS(10))==pdTRUE&&mode==3)transmit(packet.data,packet.length);
        }else if(current==2){
            /* Initial 60ms queue absorbs browser scheduling; never grow unbounded. */
            if(!primed&&uxQueueMessagesWaiting(packets)>=6)primed=true;
            if(primed&&xQueueReceive(packets,&packet,0)==pdTRUE){
                int16_t pcm[(CAPACITY-HEADER)/2];memcpy(pcm,packet.data+HEADER,packet.length-HEADER);
                play_rms=rms(pcm,(packet.length-HEADER)/2);
                if(bsp_audio_write(pcm,packet.length-HEADER)!=ESP_OK)io_errors++;
                continue;
            }
            if(primed){underrun++;primed=false;}
        }
        if(bsp_audio_write(silence,sizeof(silence))!=ESP_OK){io_errors++;vTaskDelay(pdMS_TO_TICKS(10));}
    }
}
esp_err_t audio_lab_init(void){
    packets=xQueueCreate(32,sizeof(packet_t));if(!packets)return ESP_ERR_NO_MEM;
    esp_err_t rc=bsp_audio_init();if(rc!=ESP_OK)return rc;
    rc=bsp_audio_set_format(8000,16,1);if(rc!=ESP_OK)return rc;
    bsp_audio_set_volume(90);
    if(xTaskCreate(input_task,"pcm-in",4096,NULL,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    if(xTaskCreate(output_task,"pcm-out",4096,NULL,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    return ESP_OK;
}
