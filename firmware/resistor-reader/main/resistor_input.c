#include "resistor_input.h"
void rr_input_tick(rr_input_t *in, rr_state_t *s, uint32_t now) {
    if (in->held[RR_OK] && !in->backwards && (uint32_t)(now - in->ok_since) >= RR_OK_HOLD_MS) {
        rr_handle(s, RR_OK, RR_LONG);
        in->backwards = true;
    }
}
void rr_input_event(rr_input_t *in, rr_state_t *s, rr_key_t key, rr_event_t ev, uint32_t now) {
    if (key < RR_UP || key > RR_OK) return;
    if (ev == RR_PRESS && !in->held[key]) {
        in->held[key] = true;
        if (key == RR_OK) { in->ok_since = now; in->backwards = false; }
        else rr_handle(s, key, RR_CLICK);
    } else if (ev == RR_RELEASE && in->held[key]) {
        if (key == RR_OK) {
            /* Use the release timestamp even if the UI drained this event later. */
            rr_input_tick(in, s, now);
            if (!in->backwards) rr_handle(s, RR_OK, RR_CLICK);
        }
        in->held[key] = false;
    }
    /* Delayed CLICK/DOUBLE/LONG notifications are never a second user action. */
}
