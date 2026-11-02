#ifndef HM_PRODUCT_H
#define HM_PRODUCT_H
#include "hm_core.h"

#define HM_HISTORY_MAX 5
#define HM_THEME_COUNT 4
#define HM_MENU_COUNT 7
/* BSP key order; this module does not include BSP, LVGL or ESP headers. */
typedef enum { HM_KEY_UP=0, HM_KEY_DOWN=1, HM_KEY_OK=2 } hm_product_key_t;
typedef enum { HM_KEY_PRESS, HM_KEY_RELEASE, HM_KEY_CLICK, HM_KEY_LONG } hm_product_key_event_t;
typedef enum { HM_PAGE_MAIN, HM_PAGE_MENU, HM_PAGE_HISTORY, HM_PAGE_DETAIL,
    HM_PAGE_VOLUME, HM_PAGE_PHONE, HM_PAGE_PAIR, HM_PAGE_REPLACE_CONFIRM,
    HM_PAGE_REMOVE_CONFIRM, HM_PAGE_THEME, HM_PAGE_DEVICE } hm_product_page_t;
typedef enum { HM_THEME_GREEN, HM_THEME_AMBER, HM_THEME_ICE_BLUE, HM_THEME_SOFT_WHITE } hm_theme_t;
typedef enum { HM_PAIR_IDLE, HM_PAIR_WINDOW, HM_PAIR_CONNECTING, HM_PAIR_TIMEOUT,
    HM_PAIR_FAILED, HM_PAIR_SAVED } hm_pair_state_t;
typedef enum { HM_NOTICE_NONE, HM_NOTICE_VOLUME_FAILED, HM_NOTICE_THEME_FAILED,
    HM_NOTICE_UNPAIR_FAILED, HM_NOTICE_SPECTRUM_FAILED } hm_product_notice_t;
typedef uint32_t hm_product_actions_t;
enum { HM_ACTION_NONE=0, HM_ACTION_PTT_DOWN=1u<<0, HM_ACTION_PTT_UP=1u<<1,
    HM_ACTION_PAIR_OPEN=1u<<2, HM_ACTION_PAIR_CLOSE=1u<<3,
    HM_ACTION_UNPAIR=1u<<4, HM_ACTION_UNPAIR_AND_PAIR=1u<<5 };
typedef struct { const char *id, *name; uint32_t background, primary, secondary, line; } hm_theme_palette_t;
typedef struct { uint32_t uid; char callsign[25]; uint64_t received_ms; } hm_history_entry_t;
typedef struct {
    bool (*store_volume)(uint8_t value, void *ctx);
    bool (*store_theme)(const char *stable_id, void *ctx);
    void *ctx;
    bool (*store_spectrum)(bool enabled, void *ctx);
} hm_product_store_t;
/* Public, copyable UI state. All calls and copies must use caller's coreguard.
 * volume/theme are immediate preview values; saved_* are transaction baselines.
 * dirty quick volume makes remote volume busy until settled. */
typedef struct {
    hm_product_page_t page;
    uint8_t menu_index, phone_index, detail_page, history_page, confirm_index;
    uint8_t volume, saved_volume;
    hm_theme_t theme, saved_theme;
    bool spectrum_enabled; /* Default off; only changes after persistence succeeds. */
    bool volume_dirty, bonded, secure, pair_window, observed;
    hm_pair_state_t pair_state;
    hm_product_notice_t notice;
    uint64_t notice_until_ms, volume_overlay_until_ms, volume_due_ms;
    uint64_t disconnected_ms;
    hm_snapshot_t core;
    hm_history_entry_t history[HM_HISTORY_MAX];
    uint8_t history_count;
    hm_product_store_t store;
    bool key_down[3], key_consumed[3], ok_blocked, ptt_down;
    uint64_t key_since_ms[3], repeat_due_ms[3];
} hm_product_t;

/* Display code is valid for one BLE generation and an open, uncommitted window. */
typedef struct { uint32_t generation, value; bool valid; } hm_pair_code_t;
void hm_product_pair_code_sync(hm_pair_code_t *, uint32_t generation, bool window_open, bool secure);
void hm_product_pair_code_receive(hm_pair_code_t *, uint32_t event_generation, uint32_t current_generation, uint32_t value, bool window_open, bool secure, bool on_pair_page);

const hm_theme_palette_t *hm_product_palette(hm_theme_t theme);
hm_theme_t hm_product_theme_from_id(const char *stable_id);
void hm_product_init(hm_product_t *, uint8_t saved_volume, const char *theme_id, hm_product_store_t);
/* Establish the persisted setting without invoking a store callback. */
void hm_product_load_spectrum(hm_product_t *, bool enabled);
/* observe never opens pairing. First unbonded observation shows PAIR/IDLE.
 * audio_ready denotes HELLO completion; secure alone never auto-finishes PAIR. */
void hm_product_observe(hm_product_t *, const hm_snapshot_t *, bool bonded, bool secure, bool pair_window, uint64_t now);
hm_product_actions_t hm_product_button(hm_product_t *, hm_product_key_t, hm_product_key_event_t, uint64_t now);
/* Runtime ignores BSP delayed CLICK/DOUBLE; this handles actual RELEASE once. */
hm_product_actions_t hm_product_release_click(hm_product_t *,hm_product_key_t,uint64_t now);
hm_product_actions_t hm_product_tick(hm_product_t *, uint64_t now);
/* Report synchronous unpair/open failures after applying returned action flags. */
void hm_product_action_result(hm_product_t *, hm_product_actions_t action, bool success, uint64_t now);
/* Only call after accepted received PCM, never on talker.update or mic PCM. */
void hm_product_received_audio(hm_product_t *, const hm_snapshot_t *, uint64_t now);
const hm_history_entry_t *hm_product_previous(const hm_product_t *);
void hm_product_age(uint64_t received_ms, uint64_t now, char *out, size_t bytes);
bool hm_product_volume_busy(const hm_product_t *);
/* Called by remote volume store after persistence succeeds; no callback/write. */
void hm_product_remote_volume(hm_product_t *, uint8_t value);
#endif
