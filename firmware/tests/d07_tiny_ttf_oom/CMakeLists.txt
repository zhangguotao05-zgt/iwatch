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

# 诊断邮箱与主循环排空顺序直接取生产代码，只替换RTOS和硬件时序边界。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_apps/component_gallery/iw_components_demo.c" COMPONENT_DEMO)
string(REGEX REPLACE "#include [^\n]*" "" COMPONENT_DEMO "${COMPONENT_DEMO}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/component_demo_under_test.inc" "${COMPONENT_DEMO}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_apps/watch_demo.c" WATCH_DEMO)
string(FIND "${WATCH_DEMO}" "static uint32_t gui_process_frame(void)" FRAME_START)
if(FRAME_START LESS 0)
    message(FATAL_ERROR "Cannot locate production GUI frame coordinator")
endif()
string(SUBSTRING "${WATCH_DEMO}" ${FRAME_START} -1 FRAME_REST)
string(FIND "${FRAME_REST}" "\n}" FRAME_END)
math(EXPR FRAME_LENGTH "${FRAME_END} + 2")
string(SUBSTRING "${FRAME_REST}" 0 ${FRAME_LENGTH} FRAME_FUNCTION)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gui_frame_under_test.inc" "${FRAME_FUNCTION}")

# 原样提取触摸生产队列和 GUI 取消函数；测试仅替换 RTOS/设备边界。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/platform/iw_drv_touch.c" TOUCH_DRIVER)
string(FIND "${TOUCH_DRIVER}" "#define MAX_TOUCH_REC" TOUCH_START)
string(FIND "${TOUCH_DRIVER}" "static rt_list_t driver_list;" TOUCH_END)
math(EXPR TOUCH_LENGTH "${TOUCH_END} - ${TOUCH_START}")
string(SUBSTRING "${TOUCH_DRIVER}" ${TOUCH_START} ${TOUCH_LENGTH} TOUCH_QUEUE)
string(FIND "${TOUCH_DRIVER}" "static rt_size_t touch_read(" TOUCH_READ_START)
string(FIND "${TOUCH_DRIVER}" "static rt_err_t control(" TOUCH_READ_END)
math(EXPR TOUCH_READ_LENGTH "${TOUCH_READ_END} - ${TOUCH_READ_START}")
string(SUBSTRING "${TOUCH_DRIVER}" ${TOUCH_READ_START} ${TOUCH_READ_LENGTH} TOUCH_READ)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/touch_driver_under_test.inc" "${TOUCH_QUEUE}\n${TOUCH_READ}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/platform/iw_touch_input.c" TOUCH_INPUT)
string(REGEX REPLACE "#include [^\n]*" "" TOUCH_INPUT "${TOUCH_INPUT}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/touch_input_under_test.inc" "${TOUCH_INPUT}")
string(FIND "${WATCH_DEMO}" "static void input_cancel_lvgl(void)" CANCEL_START)
string(SUBSTRING "${WATCH_DEMO}" ${CANCEL_START} -1 CANCEL_REST)
string(FIND "${CANCEL_REST}" "\n}" CANCEL_END)
math(EXPR CANCEL_LENGTH "${CANCEL_END} + 2")
string(SUBSTRING "${CANCEL_REST}" 0 ${CANCEL_LENGTH} CANCEL_FUNCTION)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/input_cancel_under_test.inc" "${CANCEL_FUNCTION}")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/platform/iw_router.c" ROUTER)
string(REGEX REPLACE "#include [^\n]*" "" ROUTER "${ROUTER}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/router_under_test.inc" "${ROUTER}")
file(READ "${SDK_ROOT}/middleware/app_fwk/app_schedule_port.c" ROUTE_SCREEN)
string(FIND "${ROUTE_SCREEN}" "static const lv_obj_class_t route_screen_class" SCREEN_START)
string(FIND "${ROUTE_SCREEN}" "\n#endif\n\n#ifdef PKG_USING_LITTLEVGL2RTT" SCREEN_END)
math(EXPR SCREEN_LENGTH "${SCREEN_END} - ${SCREEN_START}")
string(SUBSTRING "${ROUTE_SCREEN}" ${SCREEN_START} ${SCREEN_LENGTH} SCREEN_FUNCTION)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/route_screen_under_test.inc" "${SCREEN_FUNCTION}")

# SDK 受理、真实栈和快照直接编译生产函数；不复制其分支逻辑。
function(extract_sdk_function source marker output)
    file(READ "${SDK_ROOT}/${source}" text)
    string(FIND "${text}" "${marker}" begin)
    if(begin LESS 0)
        message(FATAL_ERROR "Missing SDK function: ${marker}")
    endif()
    string(SUBSTRING "${text}" ${begin} -1 rest)
    string(FIND "${rest}" "\n}" end)
    math(EXPR length "${end} + 2")
    string(SUBSTRING "${rest}" 0 ${length} body)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${output}" "${body}")
endfunction()
extract_sdk_function("middleware/app_fwk/gui_app_fwk.c" "static rt_err_t send_msg_to_gui_app_task(" "sdk_send_under_test.inc")
extract_sdk_function("middleware/app_fwk/app_schedule.c" "static rt_err_t app_create_page(" "sdk_create_under_test.inc")
extract_sdk_function("middleware/app_fwk/app_schedule.c" "void app_schedule_route_snapshot(" "sdk_snapshot_under_test.inc")

add_executable(test_tiny_ttf_oom test_tiny_ttf_oom.c test_epic_glyph.c test_font_service.c test_components.c test_component_navigation.c test_touch_input.c test_router.c test_sdk_navigation.c
    "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/services/iw_service.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/core/iw_time.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_core/iw_components.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_core/iw_gui_owner.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_core/iw_theme.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_core/iw_scope.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_core/iw_routes.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_core/iw_navigator.c")
target_include_directories(test_tiny_ttf_oom PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/gui_core")
target_include_directories(test_tiny_ttf_oom PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/platform")
target_include_directories(test_tiny_ttf_oom PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/services" "${CMAKE_CURRENT_SOURCE_DIR}/../../iwatch/src/core")
target_include_directories(test_tiny_ttf_oom PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(test_tiny_ttf_oom PRIVATE c_std_11)
if(MSVC)
    # LVGL 公共头存在固定的窄化转换告警；测试自身其余告警仍按错误处理。
    target_compile_options(test_tiny_ttf_oom PRIVATE /utf-8 /W4 /WX /wd4244 /fsanitize=address)
    target_link_options(test_tiny_ttf_oom PRIVATE /fsanitize=address)
endif()
target_link_libraries(test_tiny_ttf_oom PRIVATE lvgl_host)
