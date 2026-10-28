#include "resistor_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    rr_state_t s;
    rr_init(&s);
    assert(rr_valid(&s));
    assert(s.bands == 4 && s.selected == 1);
    assert(rr_centi_ohms(&s) == 470000);
    assert(rr_tolerance_percent(&s) == 5.0);
    assert(rr_temperature_ppm(&s) == 0);
    assert(rr_role(&s, 3) == RR_MULTIPLIER);
    assert(rr_role(&s, 4) == RR_TOLERANCE);

    s.bands = 5; s.digits[0] = RR_BROWN; s.digits[1] = RR_BLACK;
    s.digits[2] = RR_BLACK; s.multiplier = RR_RED; s.tolerance = RR_BROWN;
    assert(rr_centi_ohms(&s) == 1000000); /* 10 kOhm, 1% */
    assert(rr_tolerance_percent(&s) == 1.0);
    s.bands = 6; s.multiplier = RR_ORANGE; s.tcr = RR_RED;
    assert(rr_centi_ohms(&s) == 10000000 && rr_temperature_ppm(&s) == 50);
    assert(rr_role(&s, 6) == RR_TCR);

    s.bands = 4; s.multiplier = RR_GOLD;
    assert(rr_centi_ohms(&s) == 100);
    s.multiplier = RR_SILVER;
    assert(rr_centi_ohms(&s) == 10); /* 0.1 Ohm */
    s.bands = 6; s.digits[0] = s.digits[1] = s.digits[2] = RR_WHITE;
    s.multiplier = RR_WHITE;
    assert(rr_centi_ohms(&s) == UINT64_C(99900000000000));

    assert(!rr_color_allowed(RR_DIGIT, true, RR_BLACK));
    assert(!rr_color_allowed(RR_DIGIT, false, RR_GOLD));
    assert(!rr_color_allowed(RR_TOLERANCE, false, RR_ORANGE));
    assert(!rr_color_allowed(RR_TCR, false, RR_BLACK));
    assert(rr_color_allowed(RR_MULTIPLIER, false, RR_SILVER));
    s.digits[0] = RR_BLACK;
    assert(!rr_valid(&s));

    /* Every supported color cycle remains valid and comes back to its start. */
    for (unsigned count = 4; count <= 6; count++) {
        for (unsigned selection = 1; selection <= count; selection++) {
            rr_init(&s); s.bands = count; s.selected = selection;
            rr_color_t start = rr_color(&s, selection);
            unsigned visited = 0;
            do {
                rr_handle(&s, RR_UP, RR_CLICK);
                assert(rr_valid(&s));
                assert(++visited <= 12);
            } while (rr_color(&s, selection) != start);
            assert(visited >= 6);
            rr_handle(&s, RR_DOWN, RR_CLICK);
            rr_handle(&s, RR_UP, RR_CLICK);
            assert(rr_color(&s, selection) == start);
        }
    }
    rr_init(&s);
    rr_handle(&s, RR_UP, RR_PRESS); rr_handle(&s, RR_UP, RR_RELEASE);
    assert(s.digits[0] == RR_YELLOW);
    rr_handle(&s, RR_UP, RR_DOUBLE);
    assert(s.digits[0] == RR_BLUE);
    rr_handle(&s, RR_OK, RR_LONG);
    assert(s.bands == 4 && s.selected == 0 && rr_valid(&s));
    rr_handle(&s, RR_OK, RR_RELEASE);
    assert(s.bands == 4 && s.selected == 0);
    s.selected = 0; rr_handle(&s, RR_DOWN, RR_CLICK);
    assert(s.bands == 6);
    s.bands = 6; s.selected = 6; rr_handle(&s, RR_OK, RR_LONG);
    assert(s.bands == 6 && s.selected == 5);
    s.selected = 0; rr_handle(&s, RR_OK, RR_LONG);
    assert(s.bands == 6 && s.selected == 6);
    rr_handle(&s, RR_OK, RR_CLICK);
    assert(s.selected == 0);
    for (unsigned i = 0; i < 500; i++) {
        rr_handle(&s, (rr_key_t)(i % 3), i % 7 == 0 ? RR_LONG : RR_CLICK);
        assert(rr_valid(&s));
    }
    char number[32];
    assert(rr_format(4700, number, sizeof(number)) == 1 && !strcmp(number, "4.7"));
    assert(rr_format(.1, number, sizeof(number)) == 0 && !strcmp(number, "0.1"));
    assert(rr_format(999000000000.0, number, sizeof(number)) == 3 && !strcmp(number, "999"));
    assert(rr_format(1098900000000.0, number, sizeof(number)) == 4);
    puts("Resistor model: examples, limits, legal colors, controls and formatting PASS");
    return 0;
}
