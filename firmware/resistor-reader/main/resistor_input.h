#pragma once
#include "resistor_model.h"
#define RR_OK_HOLD_MS 400u
typedef struct { bool held[3], backwards; uint32_t ok_since; } rr_input_t;
void rr_input_event(rr_input_t *input, rr_state_t *state, rr_key_t key, rr_event_t event, uint32_t now);
void rr_input_tick(rr_input_t *input, rr_state_t *state, uint32_t now);
