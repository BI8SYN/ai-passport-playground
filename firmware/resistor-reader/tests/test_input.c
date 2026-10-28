#include "resistor_input.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    rr_state_t s; rr_init(&s); rr_input_t in = {0};
    rr_input_event(&in, &s, RR_UP, RR_PRESS, 10);
    assert(s.digits[0] == RR_GREEN); /* Acts on press, before release/180ms click window. */
    rr_input_event(&in, &s, RR_UP, RR_PRESS, 11);
    rr_input_event(&in, &s, RR_UP, RR_RELEASE, 30);
    rr_input_event(&in, &s, RR_UP, RR_CLICK, 220);
    rr_input_event(&in, &s, RR_UP, RR_DOUBLE, 230);
    assert(s.digits[0] == RR_GREEN);
    rr_input_event(&in, &s, RR_OK, RR_PRESS, 300);
    rr_input_tick(&in, &s, 699); assert(s.selected == 1);
    rr_input_event(&in, &s, RR_OK, RR_RELEASE, 699); assert(s.selected == 2);
    rr_input_event(&in, &s, RR_OK, RR_CLICK, 900); assert(s.selected == 2);
    rr_input_event(&in, &s, RR_OK, RR_PRESS, 1000);
    rr_input_tick(&in, &s, 1399); assert(s.selected == 2);
    rr_input_tick(&in, &s, 1400); assert(s.selected == 1);
    rr_input_tick(&in, &s, 2000); assert(s.selected == 1);
    rr_input_event(&in, &s, RR_OK, RR_LONG, 1500);
    rr_input_event(&in, &s, RR_OK, RR_RELEASE, 2100); assert(s.selected == 1);
    rr_input_event(&in, &s, RR_OK, RR_PRESS, 2200);
    rr_input_event(&in, &s, RR_OK, RR_RELEASE, 2600); assert(s.selected == 0);
    rr_input_event(&in, &s, RR_UP, RR_PRESS, 2700); assert(s.bands == 5);
    rr_input_event(&in, &s, RR_UP, RR_RELEASE, 2710);
    rr_input_event(&in, &s, RR_OK, RR_PRESS, UINT32_MAX - 100);
    rr_input_tick(&in, &s, 298); assert(s.selected == 0);
    rr_input_tick(&in, &s, 299); assert(s.selected == 5 && s.bands == 5);
    rr_input_event(&in, &s, RR_OK, RR_RELEASE, 300); assert(s.selected == 5);
    for (unsigned i = 0; i < 2; i++) {
        rr_input_event(&in, &s, RR_OK, RR_PRESS, 10000 + i * 50);
        rr_input_event(&in, &s, RR_OK, RR_RELEASE, 10020 + i * 50);
    }
    assert(s.selected == 1); /* Rapid taps are two next-ring actions, without waiting for double-click. */
    puts("Immediate input, 400ms backwards, release suppression, rapid taps and clock wrap: PASS");
}
