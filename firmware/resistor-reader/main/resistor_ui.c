#include "resistor_ui.h"
#include <stdio.h>

#define BG 0x111A24
#define PANEL 0x1D2B39
#define INK 0xF1F5F9
#define MUTED 0x9BAAB9
#define ACCENT 0x66E3C4

const char rr_ui_glyphs[] = "电阻色环数量第有效数字倍率容差温度系数黑棕红橙黄绿蓝紫灰白金银上下调整确定切换长按±Ω";
static const char *color_names[] = {"黑", "棕", "红", "橙", "黄", "绿", "蓝", "紫", "灰", "白", "金", "银"};
static const uint32_t colors[] = {0x16191E, 0x8D5135, 0xEB4B4B, 0xFF932B, 0xF6D74B, 0x38A768,
                                0x3F83E6, 0x9C67CD, 0x87919B, 0xF2F2EC, 0xCDA54B, 0xB9C3CD};
static const char *unit_names[] = {"Ω", "kΩ", "MΩ", "GΩ", "TΩ"};
static const char *role_names[] = {"有效数字", "倍率", "容差", "温度系数"};
static lv_obj_t *screen, *count_box, *count_label, *battery_label, *value_label, *unit_label;
static lv_obj_t *tolerance_label, *range_label, *role_label, *color_label, *swatch;
static lv_obj_t *rings[6], *markers[6];

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
    count_box = box(screen, 20, 46, 200, 29, PANEL, 8);
    count_label = label(count_box, 0, 3, 200, "", &resistor_font_16, INK, LV_TEXT_ALIGN_CENTER);

    /* Original resistor illustration: leads, ceramic body, live bands and focus marks. */
    box(screen, 12, 108, 216, 4, 0xA2AFBB, 2);
    box(screen, 32, 86, 176, 48, 0xD6C3A0, 17);
    box(screen, 38, 89, 164, 4, 0xEADCC3, 2);
    for (unsigned i = 0; i < 6; i++) {
        rings[i] = box(screen, 0, 88, 12, 44, BG, 1);
        markers[i] = box(screen, 0, 138, 12, 3, ACCENT, 1);
    }
    value_label = label(screen, 18, 150, 145, "", &lv_font_montserrat_32, INK, LV_TEXT_ALIGN_RIGHT);
    unit_label = label(screen, 171, 163, 52, "", &resistor_font_16, ACCENT, LV_TEXT_ALIGN_LEFT);
    tolerance_label = label(screen, 20, 188, 200, "", &resistor_font_16, MUTED, LV_TEXT_ALIGN_CENTER);
    range_label = label(screen, 16, 212, 208, "", &lv_font_montserrat_12, MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *editor = box(screen, 20, 234, 200, 43, PANEL, 8);
    swatch = box(editor, 11, 12, 18, 18, ACCENT, 4);
    role_label = label(editor, 39, 2, 154, "", &resistor_font_16, MUTED, LV_TEXT_ALIGN_LEFT);
    color_label = label(editor, 39, 21, 154, "", &resistor_font_16, INK, LV_TEXT_ALIGN_LEFT);
    label(screen, 20, 283, 200, "上下调整 确定切换", &resistor_font_16, MUTED, LV_TEXT_ALIGN_CENTER);
    label(screen, 20, 305, 200, "Hold OK: 4 / 5 / 6 bands", &lv_font_montserrat_12, MUTED, LV_TEXT_ALIGN_CENTER);
    lv_screen_load(screen);
    rr_ui_refresh(state, -1);
}

void rr_ui_refresh(const rr_state_t *s, int battery) {
    lv_label_set_text_fmt(count_label, "%u 环电阻", s->bands);
    lv_obj_set_style_border_width(count_box, s->selected == 0 ? 2 : 0, 0);
    lv_obj_set_style_border_color(count_box, lv_color_hex(ACCENT), 0);
    if (battery < 0) lv_label_set_text(battery_label, "--%");
    else lv_label_set_text_fmt(battery_label, "%d%%", battery);
    for (unsigned i = 0; i < 6; i++) {
        if (i >= s->bands) { lv_obj_add_flag(rings[i], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(markers[i], LV_OBJ_FLAG_HIDDEN); continue; }
        int x = 52 + (int)i * (128 / ((int)s->bands - 1));
        lv_obj_remove_flag(rings[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(rings[i], x);
        lv_obj_set_style_bg_color(rings[i], lv_color_hex(colors[rr_color(s, i + 1)]), 0);
        lv_obj_set_style_outline_width(rings[i], s->selected == i + 1 ? 2 : 0, 0);
        lv_obj_set_style_outline_pad(rings[i], 2, 0);
        lv_obj_set_style_outline_color(rings[i], lv_color_hex(ACCENT), 0);
        lv_obj_set_x(markers[i], x);
        if (s->selected == i + 1) lv_obj_remove_flag(markers[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(markers[i], LV_OBJ_FLAG_HIDDEN);
    }
    char number[32], low[32], high[32], range[96], tolerance_text[16];
    double ohms = (double)rr_centi_ohms(s) / 100;
    double tolerance = rr_tolerance_percent(s);
    snprintf(tolerance_text, sizeof(tolerance_text), "%g", tolerance);
    int unit = rr_format(ohms, number, sizeof(number));
    lv_label_set_text(value_label, number);
    lv_label_set_text(unit_label, unit_names[unit]);
    lv_label_set_text_fmt(tolerance_label, "容差 ±%s%%", tolerance_text);
    if (s->bands == 6)
        lv_label_set_text_fmt(tolerance_label, "±%s%%  %u ppm/K", tolerance_text, rr_temperature_ppm(s));
    int lower_unit = rr_format(ohms * (1 - tolerance / 100), low, sizeof(low));
    int upper_unit = rr_format(ohms * (1 + tolerance / 100), high, sizeof(high));
    static const char *ascii_units[] = {"R", "k", "M", "G", "T"};
    snprintf(range, sizeof(range), "%s%s ~ %s%s Ohm", low, ascii_units[lower_unit], high, ascii_units[upper_unit]);
    lv_label_set_text(range_label, range);
    if (!s->selected) {
        lv_label_set_text(role_label, "色环数量");
        lv_label_set_text_fmt(color_label, "%u / 4 5 6", s->bands);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(ACCENT), 0);
    } else {
        rr_role_t role = rr_role(s, s->selected);
        rr_color_t color = rr_color(s, s->selected);
        lv_label_set_text_fmt(role_label, "第%u环 %s", s->selected, role_names[role]);
        if (role == RR_DIGIT) lv_label_set_text_fmt(color_label, "%s = %u", color_names[color], (unsigned)color);
        else if (role == RR_TOLERANCE) lv_label_set_text_fmt(color_label, "%s ±%s%%", color_names[color], tolerance_text);
        else if (role == RR_TCR) lv_label_set_text_fmt(color_label, "%s %u ppm/K", color_names[color], rr_temperature_ppm(s));
        else {
            int exponent = color == RR_SILVER ? -2 : color == RR_GOLD ? -1 : (int)color;
            lv_label_set_text_fmt(color_label, "%s x10^%d", color_names[color], exponent);
        }
        lv_obj_set_style_bg_color(swatch, lv_color_hex(colors[color]), 0);
    }
}
