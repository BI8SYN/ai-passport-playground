#pragma once
#include "host/ble_gatt.h"
#include "esp_err.h"
extern uint16_t audio_lab_tx_handle;
esp_err_t audio_lab_init(void);
void audio_lab_connected(uint16_t connection);
void audio_lab_disconnected(void);
void audio_lab_subscribed(bool enabled);
int audio_lab_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
