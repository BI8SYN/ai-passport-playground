cmake_minimum_required(VERSION 3.16)
project(passport_simulator C)
set(CMAKE_C_STANDARD 11)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_DIR}" lvgl)
add_executable(passport_simulator backend.c
    "${APP_DIR}/hm_core.c" "${APP_DIR}/hm_adpcm.c" "${APP_DIR}/hm_product.c"
    "${APP_DIR}/hm_ui.c" "${APP_DIR}/hm_spectrum.c"
    "${APP_DIR}/hm_fragment.c" "${APP_DIR}/hm_font_16.c"
    "${CJSON_DIR}/cJSON.c")
target_include_directories(passport_simulator PRIVATE "${APP_DIR}" "${CJSON_DIR}")
target_link_libraries(passport_simulator PRIVATE lvgl m)
target_compile_definitions(passport_simulator PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(passport_simulator PRIVATE -Wall -Wextra -Werror)
