cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/passport.cmake")

# This is the original hardware test, not a derivative product UI.
list(APPEND EXTRA_COMPONENT_DIRS "${PASSPORT_UPSTREAM_DIR}/main")
set(SDKCONFIG_DEFAULTS
    "${PASSPORT_ROOT}/config/passport.defaults"
    "${PASSPORT_UPSTREAM_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
)
if(NOT DEFINED SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(FoloToy-AI-Passport)
