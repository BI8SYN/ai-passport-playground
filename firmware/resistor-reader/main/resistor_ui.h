#pragma once
#include "lvgl.h"
#include "resistor_model.h"
LV_FONT_DECLARE(resistor_font_16);
void rr_ui_create(const rr_state_t *state);
void rr_ui_refresh(const rr_state_t *state, int battery);
/* All non-ASCII interface characters, also used by the glyph coverage check. */
extern const char rr_ui_glyphs[];
