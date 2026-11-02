#pragma once
#include "hm_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <stdatomic.h>
esp_err_t hm_audio_init(hm_core_t *core, SemaphoreHandle_t lock, _Atomic uint32_t *generation, _Atomic bool *physical_ptt);
void hm_audio_receive(uint32_t generation, const uint8_t *bytes, size_t length);
void hm_audio_flow(uint32_t generation, const uint8_t *bytes, size_t length);
bool hm_audio_ok(void);

/* Set once before BLE starts; callback is bounded and runs under core mutex. */
void hm_audio_set_history(void (*received)(const hm_snapshot_t *,uint64_t,void *),void *ctx);
/* UI task only. Copies a bounded window under core mutex, analyzes outside it. */
void hm_audio_spectrum(uint8_t levels[16],uint8_t peaks[16]);

void hm_audio_set_spectrum_enabled(bool enabled);
/* Runtime callback invoked under core guard; does not acquire it. */
bool hm_audio_stats_json(const char *section,char out[400]);
