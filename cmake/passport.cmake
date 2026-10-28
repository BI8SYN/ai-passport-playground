# Include before ESP-IDF's project.cmake in each independent firmware project.
get_filename_component(PASSPORT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PASSPORT_UPSTREAM_DIR "${PASSPORT_ROOT}/vendor/ai-passport")
list(APPEND EXTRA_COMPONENT_DIRS "${PASSPORT_UPSTREAM_DIR}/components/bsp")

# Root components/ currently contains documentation only. Add individual shared
# component directories when they have a component CMakeLists.txt.
file(GLOB PASSPORT_LOCAL_COMPONENTS LIST_DIRECTORIES true "${PASSPORT_ROOT}/components/*")
foreach(PASSPORT_COMPONENT IN LISTS PASSPORT_LOCAL_COMPONENTS)
    if(EXISTS "${PASSPORT_COMPONENT}/CMakeLists.txt")
        list(APPEND EXTRA_COMPONENT_DIRS "${PASSPORT_COMPONENT}")
    endif()
endforeach()
