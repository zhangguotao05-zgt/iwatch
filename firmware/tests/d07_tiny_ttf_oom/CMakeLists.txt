cmake_minimum_required(VERSION 3.20)
project(iwatch_tiny_ttf_oom LANGUAGES C)

if(NOT DEFINED SDK_ROOT)
    message(FATAL_ERROR "SDK_ROOT is required")
endif()

file(TO_CMAKE_PATH "${SDK_ROOT}" SDK_ROOT)
set(LVGL_ROOT "${SDK_ROOT}/external/lvgl_v9")
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${LVGL_ROOT}/src/*.c")

add_library(lvgl_host STATIC ${LVGL_SOURCES})
target_compile_features(lvgl_host PRIVATE c_std_11)
target_compile_definitions(lvgl_host PUBLIC LV_CONF_INCLUDE_SIMPLE)
if(MSVC)
    target_compile_options(lvgl_host PRIVATE /utf-8 /fsanitize=address)
endif()
target_include_directories(lvgl_host PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${LVGL_ROOT}"
)

# 编译真实字形适配及连续混合函数；仅替换硬件边界，不复制被测算法。
file(READ "${SDK_ROOT}/middleware/lvgl/lv_drivers_v9/sifli/lv_draw_epic_label.c" EPIC_LABEL)
string(REGEX REPLACE "#include [^\n]*" "" EPIC_LABEL "${EPIC_LABEL}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/epic_label_under_test.inc" "${EPIC_LABEL}")
file(READ "${SDK_ROOT}/rtos/rtthread/bsp/sifli/drivers/drv_epic_single.c" EPIC_DRIVER)
string(FIND "${EPIC_DRIVER}" "rt_err_t drv_epic_cont_blend(" EPIC_START)
string(FIND "${EPIC_DRIVER}" "void drv_epic_cont_blend_reset(void)" EPIC_END)
if(EPIC_START LESS 0 OR EPIC_END LESS EPIC_START)
    message(FATAL_ERROR "Cannot locate production EPIC continuous blend function")
endif()
math(EXPR EPIC_LENGTH "${EPIC_END} - ${EPIC_START}")
string(SUBSTRING "${EPIC_DRIVER}" ${EPIC_START} ${EPIC_LENGTH} EPIC_BLEND)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/epic_driver_under_test.inc" "${EPIC_BLEND}")

add_executable(test_tiny_ttf_oom test_tiny_ttf_oom.c test_epic_glyph.c test_font_service.c
    "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_core/iw_theme.c")
target_include_directories(test_tiny_ttf_oom PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_core")
target_include_directories(test_tiny_ttf_oom PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(test_tiny_ttf_oom PRIVATE c_std_11)
if(MSVC)
    # LVGL 公共头存在固定的窄化转换告警；测试自身其余告警仍按错误处理。
    target_compile_options(test_tiny_ttf_oom PRIVATE /utf-8 /W4 /WX /wd4244 /fsanitize=address)
    target_link_options(test_tiny_ttf_oom PRIVATE /fsanitize=address)
endif()
target_link_libraries(test_tiny_ttf_oom PRIVATE lvgl_host)
