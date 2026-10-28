#include "resistor_ui.h"
#include "resistor_render.h"
#include <stdio.h>

#define BG 0x111A24
#define PANEL 0x1D2B39
#define INK 0xF1F5F9
#define MUTED 0x9BAAB9
#define ACCENT 0x66E3C4

const char rr_ui_glyphs[] = "电阻色环数量第有效数字倍率容差温度系数上下调整确定切换长按选下一环上·±Ω";
static const char *unit_names[] = {"Ω", "kΩ", "MΩ", "GΩ", "TΩ"};
static const char *role_names[] = {"有效数字", "倍率", "容差", "温度系数"};
static lv_obj_t *screen, *count_box, *count_label, *battery_label, *value_label, *unit_label;
static lv_obj_t *tolerance_label, *range_label, *role_label;
static lv_obj_t *resistor_image, *markers[6];
static uint16_t resistor_pixels[RR_IMAGE_WIDTH * RR_IMAGE_HEIGHT] __attribute__((aligned(4)));
static rr_state_t previous;
static bool previous_valid;
static int previous_battery = -999;

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int width, int height, uint32_t color, int radius) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const char *text,
                       const lv_font_t *font, uint32_t color, lv_text_align_t align) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text);
    return obj;
}

void rr_ui_create(const rr_state_t *state) {
    screen = box(NULL, 0, 0, 240, 320, BG, 30);
    lv_obj_set_style_text_font(screen, &resistor_font_16, 0);
    label(screen, 20, 17, 145, "电阻色环", &resistor_font_16, INK, LV_TEXT_ALIGN_LEFT);
    battery_label = label(screen, 174, 19, 44, "--%", &lv_font_montserrat_12, MUTED, LV_TEXT_ALIGN_RIGHT);
    count_box = box(screen, 86, 49, 68, 27, PANEL, 8);
    count_label = label(count_box, 0, 2, 68, "", &resistor_font_16, INK, LV_TEXT_ALIGN_CENTER);

    resistor_image = lv_canvas_create(screen);
    lv_obj_set_pos(resistor_image, 0, 82);
    lv_canvas_set_buffer(resistor_image, resistor_pixels, RR_IMAGE_WIDTH, RR_IMAGE_HEIGHT, LV_COLOR_FORMAT_RGB565);
    for (unsigned i = 0; i < 6; i++) {
        markers[i] = box(screen, 0, 148, 9, 2, ACCENT, 1);
    }
    value_label = label(screen, 0, 174, 145, "", &lv_font_montserrat_32, INK, LV_TEXT_ALIGN_RIGHT);
    unit_label = label(screen, 0, 187, 52, "", &resistor_font_16, ACCENT, LV_TEXT_ALIGN_LEFT);
    tolerance_label = label(screen, 20, 212, 200, "", &resistor_font_16, MUTED, LV_TEXT_ALIGN_CENTER);
    range_label = label(screen, 16, 235, 208, "", &lv_font_montserrat_12, MUTED, LV_TEXT_ALIGN_CENTER);
    role_label = label(screen, 20, 259, 200, "", &resistor_font_16, MUTED, LV_TEXT_ALIGN_CENTER);
    label(screen, 20, 293, 200, "确定下一环 · 长按上一环", &resistor_font_16, MUTED, LV_TEXT_ALIGN_CENTER);
    lv_screen_load(screen);
    previous_valid = false;
    previous_battery = -999;
    rr_ui_refresh(state, -1);
}

void rr_ui_refresh(const rr_state_t *s, int battery) {
    bool first = !previous_valid;
    bool count_changed = first || previous.bands != s->bands;
    bool focus_changed = first || count_changed || previous.selected != s->selected;
    if (count_changed) lv_label_set_text_fmt(count_label, "%u 环", s->bands);
    if (focus_changed) {
        lv_obj_set_style_border_width(count_box, s->selected == 0 ? 2 : 0, 0);
        lv_obj_set_style_border_color(count_box, lv_color_hex(ACCENT), 0);
    }
    if (battery != previous_battery) {
        if (battery < 0) lv_label_set_text(battery_label, "--%");
        else lv_label_set_text_fmt(battery_label, "%d%%", battery);
        previous_battery = battery;
    }
    rr_rect_t damage;
    if (rr_render_update(resistor_pixels, first ? NULL : &previous, s, &damage)) {
        lv_area_t area = {damage.x1, damage.y1 + 82, damage.x2, damage.y2 + 82};
        lv_obj_invalidate_area(resistor_image, &area);
    }
    if (focus_changed) {
        for (unsigned i = 0; i < 6; i++) {
            if (i >= s->bands || s->selected != i + 1) lv_obj_add_flag(markers[i], LV_OBJ_FLAG_HIDDEN);
            else {
                lv_obj_set_x(markers[i], rr_ring_x(s->bands, i + 1) - 4);
                lv_obj_remove_flag(markers[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (!s->selected) lv_label_set_text(role_label, "色环数量");
        else lv_label_set_text_fmt(role_label, "第%u环 · %s", s->selected, role_names[rr_role(s, s->selected)]);
    }
    bool value_changed = first || rr_centi_ohms(&previous) != rr_centi_ohms(s);
    bool tolerance_changed = first || previous.tolerance != s->tolerance;
    bool tcr_changed = first || previous.bands != s->bands || previous.tcr != s->tcr;
    if (value_changed || tolerance_changed || tcr_changed) {
        char number[32], low[32], high[32], range[96], tolerance_text[16];
        double ohms = (double)rr_centi_ohms(s) / 100;
        double tolerance = rr_tolerance_percent(s);
        if (value_changed) {
            int unit = rr_format(ohms, number, sizeof(number));
            lv_label_set_text(value_label, number);
            lv_label_set_text(unit_label, unit_names[unit]);
            lv_obj_set_width(value_label, LV_SIZE_CONTENT);
            lv_obj_set_width(unit_label, LV_SIZE_CONTENT);
            lv_obj_update_layout(screen);
            int number_width = lv_obj_get_width(value_label), unit_width = lv_obj_get_width(unit_label);
            int left = (240 - number_width - 8 - unit_width) / 2;
            lv_obj_set_x(value_label, left);
            lv_obj_set_x(unit_label, left + number_width + 8);
        }
        if (tolerance_changed || tcr_changed) {
            snprintf(tolerance_text, sizeof(tolerance_text), "%g", tolerance);
            if (s->bands == 6)
                lv_label_set_text_fmt(tolerance_label, "±%s%%  %u ppm/K", tolerance_text, rr_temperature_ppm(s));
            else lv_label_set_text_fmt(tolerance_label, "±%s%%", tolerance_text);
        }
        if (value_changed || tolerance_changed) {
            int lower_unit = rr_format(ohms * (1 - tolerance / 100), low, sizeof(low));
            int upper_unit = rr_format(ohms * (1 + tolerance / 100), high, sizeof(high));
            static const char *ascii_units[] = {"R", "k", "M", "G", "T"};
            snprintf(range, sizeof(range), "%s%s ~ %s%s Ohm", low, ascii_units[lower_unit], high, ascii_units[upper_unit]);
            lv_label_set_text(range_label, range);
        }
    }
    previous = *s;
    previous_valid = true;
}
