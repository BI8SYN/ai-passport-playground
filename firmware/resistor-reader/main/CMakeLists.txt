idf_component_register(
    SRCS "main.c" "resistor_model.c" "resistor_ui.c" "resistor_render.c" "resistor_layout.c" "resistor_input.c" "resistor_font_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp lvgl__lvgl esp_timer
)

target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../../../assets/resistor/resistor_textures.bin" BINARY)
if(RR_BENCHMARK)
    target_sources(${COMPONENT_LIB} PRIVATE "resistor_benchmark.c" "../../../tools/renderer/resistor_art.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RR_BENCHMARK=1)
endif()
