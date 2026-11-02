idf_component_register(SRCS "main.c" "hm_core.c" "hm_adpcm.c" "hm_ble.c" "hm_fragment.c" "hm_cccd_guard.c" "hm_audio.c" "hm_spectrum.c" "hm_product.c" "hm_ui.c" "hm_font_16.c"
    INCLUDE_DIRS "." REQUIRES bsp lvgl__lvgl bt nvs_flash esp_timer json PRIV_REQUIRES esp_app_format)

# ESP-IDF 5.5.3 NimBLE exposes CCCD write security but not read security.
# Narrow guarded ABI adapter hardens registration without modifying vendor.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ble_att_svr_register")
