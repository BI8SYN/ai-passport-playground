#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { RR_BLACK, RR_BROWN, RR_RED, RR_ORANGE, RR_YELLOW, RR_GREEN,
               RR_BLUE, RR_VIOLET, RR_GREY, RR_WHITE, RR_GOLD, RR_SILVER } rr_color_t;
typedef enum { RR_DIGIT, RR_MULTIPLIER, RR_TOLERANCE, RR_TCR } rr_role_t;
typedef enum { RR_UP, RR_DOWN, RR_OK } rr_key_t;
typedef enum { RR_CLICK, RR_LONG, RR_DOUBLE, RR_PRESS, RR_RELEASE } rr_event_t;
typedef struct {
    unsigned bands, selected; /* 0 = band-count control, 1..bands = color ring */
    rr_color_t digits[3], multiplier, tolerance, tcr;
} rr_state_t;

void rr_init(rr_state_t *state);
bool rr_valid(const rr_state_t *state);
rr_role_t rr_role(const rr_state_t *state, unsigned ring);
rr_color_t rr_color(const rr_state_t *state, unsigned ring);
bool rr_color_allowed(rr_role_t role, bool first_digit, rr_color_t color);
void rr_handle(rr_state_t *state, rr_key_t key, rr_event_t event);
uint64_t rr_centi_ohms(const rr_state_t *state);
double rr_tolerance_percent(const rr_state_t *state);
unsigned rr_temperature_ppm(const rr_state_t *state);
/* Unit index: 0 Ohm, 1 kOhm, 2 MOhm, 3 GOhm, 4 TOhm. */
int rr_format(double ohms, char *number, size_t size);
