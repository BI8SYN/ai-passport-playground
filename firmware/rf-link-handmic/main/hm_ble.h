#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "esp_err.h"

/* Callbacks run on the NimBLE host and must only copy/enqueue; never block.
 * authenticated means trusted bond + authenticated SC encryption + all 3 CCCDs.
 * Each loss of security/subscriptions invalidates generation, as does disconnect. */
typedef struct {
    void (*link)(uint32_t generation, bool connected, bool authenticated, uint16_t max_value);
    void (*control)(uint32_t generation, const uint8_t *json, size_t len);
    void (*audio)(uint32_t generation, const uint8_t *bytes, size_t len);
    void (*flow)(uint32_t generation, const uint8_t *bytes, size_t len);
    void (*passkey)(uint32_t generation, uint32_t six_digits);
    void (*fragment_error)(uint32_t generation);
} hm_ble_callbacks_t;
esp_err_t hm_ble_init(const char *device_id, const hm_ble_callbacks_t *callbacks);
/* Physical UI authorization only. enabled opens/restarts a 60-second window. */
void hm_ble_pairing(bool enabled);
/* Physical confirmed local unpair. Disables access immediately, then clears store. */
void hm_ble_unpair(void);
void hm_ble_poll(uint64_t now_ms);
bool hm_ble_send_control(uint32_t generation, const char *json, size_t len, uint16_t fragment_value_limit);
bool hm_ble_notify_audio(uint32_t generation, const uint8_t *bytes, size_t len);
/* Call from runtime task after stopping producer; synchronizes against submit.
 * Packets already accepted by NimBLE/controller cannot be recalled. */
void hm_ble_clear_audio(uint32_t generation);
bool hm_ble_notify_flow(uint32_t generation, const uint8_t *bytes, size_t len);
bool hm_ble_set_info(const char *json);
bool hm_ble_set_status(const char *json);
bool hm_ble_bonded(void);
bool hm_ble_connected(void);
bool hm_ble_authenticated(void);
bool hm_ble_pairing_open(void);
uint32_t hm_ble_generation(void);
uint16_t hm_ble_max_value(void);
typedef enum { HM_BLE_CRITICAL, HM_BLE_RESPONSE, HM_BLE_DISPLAY } hm_ble_priority_t;
bool hm_ble_send_control_priority(uint32_t generation, const char *json, size_t len, uint16_t fragment_value_limit, hm_ble_priority_t priority);

/* Set once after init to a boot-lifetime atomic mic permission latch. NULL fails
 * closed. RELEASE must close the latch then revoke immediately; a new physical
 * press alone must not reopen it before a fresh mic stream is permitted. */
void hm_ble_set_audio_gate(const _Atomic bool *permitted);
/* Only increments the atomic audio epoch: safe in the button callback, no lock,
 * queue work or host wait. Queued frames retain their old epoch across repress. */
void hm_ble_revoke_audio(void);
/* Boot-lifetime uint32 modulo counters. submitted=NimBLE accepted, not controller
 * delivery/phone consumption; dropped counts frames accepted into BLE queue then
 * discarded (gate, clear, expiry, stale generation or local submit failure).
 * Enqueue refusals are excluded. Output pointers may be NULL. */
void hm_ble_get_audio_stats(uint32_t *submitted, uint32_t *dropped);

/* Last local removal: -2 no request, 0 pending, 1 cleared, -1 failed. */
int hm_ble_unpair_result(void);

/* Host sync/bond discovery complete. Safe to choose first-run UI. */
bool hm_ble_ready(void);
