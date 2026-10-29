idf_component_register(
    SRCS "main.c" "ear_model.c" "ear_controller.c" "ear_synth.c" "ear_ui.c" "ear_font_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp lvgl__lvgl esp_timer esp_system nvs_flash
)
