#include "resistor_ui.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct { bool battery; int value; bsp_btn_t button; bsp_btn_ev_t event; } app_event_t;
static QueueHandle_t events;
static rr_state_t state;
static int battery_percent = -1;

static void on_button(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (event != BSP_BTN_CLICK && event != BSP_BTN_LONG && event != BSP_BTN_DOUBLE) return;
    const app_event_t message = {.button = button, .event = event};
    (void)xQueueSend(events, &message, 0);
}

/* Runs inside LVGL's task: callbacks never render or access I2C. */
static void poll_input(lv_timer_t *timer) {
    (void)timer;
    app_event_t message;
    bool changed = false;
    for (unsigned i = 0; i < 16 && xQueueReceive(events, &message, 0) == pdTRUE; i++) {
        if (message.battery) battery_percent = message.value;
        else {
            rr_key_t key = message.button == BSP_BTN_UP ? RR_UP : message.button == BSP_BTN_DOWN ? RR_DOWN : RR_OK;
            rr_event_t event = message.event == BSP_BTN_LONG ? RR_LONG : message.event == BSP_BTN_DOUBLE ? RR_DOUBLE : RR_CLICK;
            rr_handle(&state, key, event);
        }
        changed = true;
    }
    if (changed) rr_ui_refresh(&state, battery_percent);
}

void app_main(void) {
    rr_init(&state);
    events = xQueueCreate(16, sizeof(app_event_t));
    if (!events) { ESP_LOGE("resistor", "Input queue allocation failed"); return; }
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE("resistor", "Display initialization failed");
        vQueueDelete(events); events = NULL; return;
    }
    if (!bsp_lvgl_lock(1000)) { ESP_LOGE("resistor", "LVGL lock failed"); return; }
    rr_ui_create(&state);
    lv_timer_create(poll_input, 20, NULL);
    bsp_lvgl_unlock();
    bsp_display_backlight(85);
    ESP_ERROR_CHECK(bsp_button_init(on_button, NULL));
    const bool battery_available = bsp_battery_init() == ESP_OK;
    ESP_LOGI("resistor", "Ready: 4/5/6 bands; UP/DOWN edit, OK select, hold OK band count");
    for (;;) {
        const app_event_t message = {.battery = true, .value = battery_available ? bsp_battery_soc() : -1};
        (void)xQueueSend(events, &message, 0);
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}
