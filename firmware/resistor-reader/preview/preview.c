#include "resistor_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t pixels[240 * 320], draw_buffer[240 * 20];
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    const uint16_t *source = (const uint16_t *)data;
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++) pixels[y * 240 + x] = *source++;
    lv_display_flush_ready(display);
}
static void check_glyph(uint32_t code) {
    lv_font_glyph_dsc_t descriptor;
    if (!lv_font_get_glyph_dsc(&resistor_font_16, &descriptor, code, 0) || descriptor.is_placeholder) {
        fprintf(stderr, "Missing glyph U+%04X\n", (unsigned)code);
        exit(1);
    }
}
static bool has_label(lv_obj_t *root, const char *text) {
    if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return true;
    for (unsigned i = 0; i < lv_obj_get_child_count(root); i++)
        if (has_label(lv_obj_get_child(root, (int)i), text)) return true;
    return false;
}
static void save(const char *directory, unsigned bands) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%u-band.ppm", directory, bands);
    FILE *output = fopen(path, "wb");
    assert(output);
    fprintf(output, "P6\n240 320\n255\n");
    for (unsigned i = 0; i < 240 * 320; i++) {
        uint16_t pixel = pixels[i];
        const unsigned char rgb[] = {((pixel >> 11) & 31) * 255 / 31,
            ((pixel >> 5) & 63) * 255 / 63, (pixel & 31) * 255 / 31};
        assert(fwrite(rgb, 1, 3, output) == 3);
    }
    assert(fclose(output) == 0);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    lv_init();
    for (unsigned c = 32; c < 127; c++) check_glyph(c);
    const unsigned char *p = (const unsigned char *)rr_ui_glyphs;
    while (*p) {
        uint32_t c = *p++;
        if (c >= 0xE0) { c = (c & 15) << 12; c |= (*p++ & 63) << 6; c |= *p++ & 63; }
        else if (c >= 0xC0) { c = (c & 31) << 6; c |= *p++ & 63; }
        check_glyph(c);
    }
    lv_display_t *display = lv_display_create(240, 320);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    rr_state_t state;
    rr_init(&state);
    rr_ui_create(&state);
    for (unsigned bands = 4; bands <= 6; bands++) {
        state.bands = bands;
        state.selected = bands == 4 ? 1 : bands;
        rr_ui_refresh(&state, 86);
        lv_refr_now(display);
        /* Catch unsupported LVGL float formatting before delivering a screen. */
        assert(has_label(lv_screen_active(), bands == 6 ? "±5%  100 ppm/K" : "容差 ±5%"));
        save(argv[1], bands);
    }
    /* Exercise all roles and legal colors under the device's 32 KiB LVGL heap. */
    for (unsigned i = 0; i < 500; i++) {
        rr_handle(&state, i % 13 == 0 ? RR_OK : RR_UP, i % 37 == 0 ? RR_LONG : RR_CLICK);
        assert(rr_valid(&state));
        rr_ui_refresh(&state, 86);
        lv_refr_now(display);
    }
    puts("LVGL preview and ASCII/Chinese/Ohm glyph coverage: PASS");
    lv_deinit();
    return 0;
}
