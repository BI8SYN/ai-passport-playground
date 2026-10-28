idf_component_register(
    SRCS "main.c" "resistor_model.c" "resistor_ui.c" "resistor_font_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp lvgl__lvgl
)
