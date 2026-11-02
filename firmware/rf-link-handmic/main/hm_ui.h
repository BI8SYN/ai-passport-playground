#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "hm_product.h"

/* Copy under coreguard; render under BSP LVGL lock. No protocol/storage actions. */
typedef enum { HM_UI_IDLE, HM_UI_RECEIVING, HM_UI_TRANSMITTING, HM_UI_WAITING, HM_UI_ERROR } hm_ui_phase_t;
typedef struct {
    hm_product_t p;
    hm_ui_phase_t phase;
    uint64_t now_ms;
    uint8_t spectrum[16], peaks[16]; /* normalized 0..100 */
    uint32_t tx_elapsed_s, passkey;
    uint8_t pair_seconds;
    bool passkey_valid, audio_fault, settings_fault;
    /* Root formats known/expired/approximate values; empty displays a dash. */
    char distance[40], position[64], frequency[64], grid[32];
    char rig[64], antenna[64], height[40], age[48], message[128];
    char device_name[64], firmware_version[40];
} hm_ui_snapshot_t;
LV_FONT_DECLARE(hm_font_16);
/* Shared runtime/host adapter; preserve fault flags and non-PTT transient message. */
void hm_ui_set_phase(hm_ui_snapshot_t *snapshot);
#ifdef HM_UI_TESTING
uint32_t hm_ui_fast_render_count(void);
uint32_t hm_ui_full_render_count(void);
#endif
void hm_ui_create(void);
void hm_ui_render(const hm_ui_snapshot_t *snapshot);
