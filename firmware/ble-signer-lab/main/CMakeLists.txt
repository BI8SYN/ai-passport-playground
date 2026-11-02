idf_component_register(SRCS "main.c" "lab_model.c" "audio_lab.c" INCLUDE_DIRS "."
    REQUIRES bsp lvgl__lvgl bt nvs_flash esp_timer json espressif__libsodium)
