/* Diagnostic build only: compare exact old art and optimized paths on this C3. */
#include "resistor_ui.h"
#include "resistor_render.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
void rr_art_render(uint16_t *, const rr_state_t *, bool);
void rr_benchmark_run(void) {
    uint16_t *reference = malloc(RR_IMAGE_WIDTH * RR_IMAGE_HEIGHT * 2);
    uint16_t *fast = malloc(RR_IMAGE_WIDTH * RR_IMAGE_HEIGHT * 2);
    assert(reference && fast);
    rr_state_t s; rr_init(&s);
    for (unsigned bands = 4; bands <= 6; bands++) {
        s.bands = bands;
        int64_t start = esp_timer_get_time();
        rr_art_render(reference, &s, true);
        int64_t old_us = esp_timer_get_time() - start;
        vTaskDelay(pdMS_TO_TICKS(20));
        start = esp_timer_get_time();
        for (unsigned i = 0; i < 100; i++) rr_render_resistor(fast, &s);
        int64_t full_us = esp_timer_get_time() - start;
        assert(!memcmp(reference, fast, RR_IMAGE_WIDTH * RR_IMAGE_HEIGHT * 2));
        start = esp_timer_get_time();
        for (unsigned i = 0; i < 100; i++) {
            rr_state_t old = s;
            s.digits[0] = s.digits[0] == RR_YELLOW ? RR_GREEN : RR_YELLOW;
            rr_rect_t damage;
            assert(rr_render_update(fast, &old, &s, &damage));
        }
        int64_t partial_us = esp_timer_get_time() - start;
        ESP_LOGI("rr_perf", "bands=%u old_full_us=%lld fast_full_avg_us=%lld fast_color_avg_us=%lld exact_pixels=PASS",
                 bands, (long long)old_us, (long long)(full_us / 100), (long long)(partial_us / 100));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    free(reference); free(fast);
    assert(bsp_lvgl_lock(1000));
    rr_init(&s);
    rr_ui_refresh(&s, -1); lv_refr_now(NULL);
    for (unsigned action = 0; action < 3; action++) {
        const int64_t start = esp_timer_get_time();
        if (action == 0) rr_handle(&s, RR_UP, RR_CLICK);
        if (action == 1) { s.selected = 0; rr_handle(&s, RR_UP, RR_CLICK); }
        if (action == 2) rr_handle(&s, RR_OK, RR_LONG);
        rr_ui_refresh(&s, -1);
        int64_t ui_us = esp_timer_get_time() - start;
        lv_refr_now(NULL);
        ESP_LOGI("rr_perf", "action=%s ui_us=%lld ui_and_refr_submit_us=%lld",
                 action == 0 ? "color" : action == 1 ? "bands" : "focus", (long long)ui_us,
                 (long long)(esp_timer_get_time() - start));
    }
    rr_init(&s); rr_ui_refresh(&s, -1); lv_refr_now(NULL);
    bsp_lvgl_unlock();
    ESP_LOGI("rr_perf", "BENCHMARK COMPLETE (refresh submit is not physical photon latency)");
}
