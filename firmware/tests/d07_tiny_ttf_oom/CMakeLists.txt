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
    target_compile_options(lvgl_host PRIVATE /utf-8)
endif()
target_include_directories(lvgl_host PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${LVGL_ROOT}"
)

add_executable(test_tiny_ttf_oom test_tiny_ttf_oom.c)
target_compile_features(test_tiny_ttf_oom PRIVATE c_std_11)
if(MSVC)
    # LVGL 公共头存在固定的窄化转换告警；测试自身其余告警仍按错误处理。
    target_compile_options(test_tiny_ttf_oom PRIVATE /utf-8 /W4 /WX /wd4244 /fsanitize=address)
    target_link_options(test_tiny_ttf_oom PRIVATE /fsanitize=address)
endif()
target_link_libraries(test_tiny_ttf_oom PRIVATE lvgl_host)
