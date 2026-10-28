#include "resistor_model.h"
#include <math.h>
#include <stdio.h>

static const rr_color_t tolerance_colors[] = {
    RR_BROWN, RR_RED, RR_GREEN, RR_BLUE, RR_VIOLET, RR_GREY, RR_GOLD, RR_SILVER
};
/* Values explicitly listed in Vishay document 20143; no undefined TCR colors. */
static const rr_color_t tcr_colors[] = {RR_BROWN, RR_RED, RR_ORANGE, RR_YELLOW, RR_BLUE, RR_VIOLET};

void rr_init(rr_state_t *s) {
    *s = (rr_state_t){.bands = 4, .selected = 1,
        .digits = {RR_YELLOW, RR_VIOLET, RR_BLACK}, .multiplier = RR_RED,
        .tolerance = RR_GOLD, .tcr = RR_BROWN};
}

bool rr_color_allowed(rr_role_t role, bool first, rr_color_t color) {
    if (role == RR_DIGIT) return color >= (first ? RR_BROWN : RR_BLACK) && color <= RR_WHITE;
    if (role == RR_MULTIPLIER) return color >= RR_BLACK && color <= RR_SILVER;
    const rr_color_t *colors = role == RR_TOLERANCE ? tolerance_colors : tcr_colors;
    size_t count = role == RR_TOLERANCE ? sizeof(tolerance_colors) / sizeof(*colors)
                                      : sizeof(tcr_colors) / sizeof(*colors);
    if (role != RR_TOLERANCE && role != RR_TCR) return false;
    for (size_t i = 0; i < count; i++) if (colors[i] == color) return true;
    return false;
}

rr_role_t rr_role(const rr_state_t *s, unsigned ring) {
    unsigned digits = s->bands == 4 ? 2 : 3;
    if (ring <= digits) return RR_DIGIT;
    if (ring == digits + 1) return RR_MULTIPLIER;
    if (ring == digits + 2) return RR_TOLERANCE;
    return RR_TCR;
}

rr_color_t rr_color(const rr_state_t *s, unsigned ring) {
    if (ring == 0 || ring > s->bands) return RR_BLACK;
    switch (rr_role(s, ring)) {
        case RR_DIGIT: return s->digits[ring - 1];
        case RR_MULTIPLIER: return s->multiplier;
        case RR_TOLERANCE: return s->tolerance;
        case RR_TCR: return s->tcr;
    }
    return RR_BLACK;
}

bool rr_valid(const rr_state_t *s) {
    if (!s || s->bands < 4 || s->bands > 6 || s->selected > s->bands) return false;
    for (unsigned i = 1; i <= s->bands; i++)
        if (!rr_color_allowed(rr_role(s, i), i == 1, rr_color(s, i))) return false;
    return rr_color_allowed(RR_DIGIT, false, s->digits[2]);
}

static void change_count(rr_state_t *s, int delta) {
    s->bands = 4 + (unsigned)((int)s->bands - 4 + delta + 3) % 3;
    if (s->selected > s->bands) s->selected = 0;
}

static void edit(rr_state_t *s, int direction) {
    if (!s->selected) { change_count(s, direction); return; }
    unsigned ring = s->selected;
    rr_role_t role = rr_role(s, ring);
    rr_color_t color = rr_color(s, ring);
    do {
        color = (rr_color_t)(((int)color + direction + 12) % 12);
    } while (!rr_color_allowed(role, ring == 1, color));
    switch (role) {
        case RR_DIGIT: s->digits[ring - 1] = color; break;
        case RR_MULTIPLIER: s->multiplier = color; break;
        case RR_TOLERANCE: s->tolerance = color; break;
        case RR_TCR: s->tcr = color; break;
    }
}

void rr_handle(rr_state_t *s, rr_key_t key, rr_event_t event) {
    if (!rr_valid(s) || key < RR_UP || key > RR_OK) return;
    if (event == RR_LONG) { if (key == RR_OK) s->selected = (s->selected + s->bands) % (s->bands + 1); return; }
    if (event != RR_CLICK && event != RR_DOUBLE) return;
    for (unsigned i = 0; i < (event == RR_DOUBLE ? 2u : 1u); i++) {
        if (key == RR_OK) s->selected = (s->selected + 1) % (s->bands + 1);
        else edit(s, key == RR_UP ? 1 : -1);
    }
}

uint64_t rr_centi_ohms(const rr_state_t *s) {
    if (!rr_valid(s)) return 0;
    uint64_t digits = (uint64_t)s->digits[0] * 10 + s->digits[1];
    if (s->bands != 4) digits = digits * 10 + s->digits[2];
    int exponent = s->multiplier == RR_SILVER ? -2 : s->multiplier == RR_GOLD ? -1 : (int)s->multiplier;
    for (int i = 0; i < exponent + 2; i++) digits *= 10;
    return digits;
}

double rr_tolerance_percent(const rr_state_t *s) {
    static const double values[] = {1, 2, .5, .25, .1, .05, 5, 10};
    for (size_t i = 0; i < sizeof(tolerance_colors) / sizeof(*tolerance_colors); i++)
        if (s->tolerance == tolerance_colors[i]) return values[i];
    return 0;
}

unsigned rr_temperature_ppm(const rr_state_t *s) {
    static const unsigned values[] = {100, 50, 15, 25, 10, 5};
    if (s->bands != 6) return 0;
    for (size_t i = 0; i < sizeof(tcr_colors) / sizeof(*tcr_colors); i++)
        if (s->tcr == tcr_colors[i]) return values[i];
    return 0;
}

int rr_format(double ohms, char *number, size_t size) {
    int unit = 0;
    if (!isfinite(ohms) || ohms < 0) { snprintf(number, size, "--"); return 0; }
    while (ohms >= 1000 && unit < 4) { ohms /= 1000; unit++; }
    snprintf(number, size, "%.6g", ohms);
    return unit;
}
