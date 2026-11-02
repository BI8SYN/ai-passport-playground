#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
/* Only tracks mic frames after the BLE layer has accepted them into its queue.
 * Boot-lifetime counters use uint32 modulo arithmetic. */
typedef struct {
    _Atomic(const _Atomic bool *) pressed;
    _Atomic uint32_t submitted, dropped, epoch;
} hm_tx_guard_t;
static inline void hm_tx_guard_set(hm_tx_guard_t *g,const _Atomic bool *pressed) { atomic_store(&g->pressed,pressed); }
static inline uint32_t hm_tx_guard_epoch(const hm_tx_guard_t *g) { return atomic_load(&g->epoch); }
static inline void hm_tx_guard_revoke(hm_tx_guard_t *g) { atomic_fetch_add(&g->epoch,1); }
/* Final local submission boundary; denied queued frames count as drops once. */
static inline bool hm_tx_guard_submit_allowed(hm_tx_guard_t *g,uint32_t epoch) {
    const _Atomic bool *pressed=atomic_load(&g->pressed);
    /* Epoch is read last, catching a release/repress even when the latch is true. */
    if (pressed && atomic_load(pressed) && epoch==atomic_load(&g->epoch)) return true;
    atomic_fetch_add(&g->dropped,1);return false;
}
static inline bool hm_tx_guard_expired(uint64_t queued_ms,uint64_t now_ms) { return now_ms>=queued_ms && now_ms-queued_ms>120; }
static inline void hm_tx_guard_drop(hm_tx_guard_t *g,uint32_t count) { atomic_fetch_add(&g->dropped,count); }
static inline void hm_tx_guard_result(hm_tx_guard_t *g,bool submitted) {
    if (submitted) atomic_fetch_add(&g->submitted,1);else hm_tx_guard_drop(g,1);
}
static inline void hm_tx_guard_stats(const hm_tx_guard_t *g,uint32_t *submitted,uint32_t *dropped) {
    if (submitted) *submitted=atomic_load(&g->submitted);
    if (dropped) *dropped=atomic_load(&g->dropped);
}
