#include "iw_components_demo.h"
#include "iw_components.h"
#include "iw_components_demo_text.h"
#include "iw_gui_owner.h"
#include "iw_font_port.h"
#include "iw_gui_port.h"
#include <rtthread.h>
#include <rthw.h>

enum { DEMO_IDLE = -1, DEMO_CLOSE, DEMO_Q0, DEMO_Q1, DEMO_64, DEMO_80, DEMO_96, DEMO_FAULT };
static volatile int requested = DEMO_IDLE;
static iw_component_t frame, row, button, panel, banner;

static void request(int command)
{
    rt_base_t level = rt_hw_interrupt_disable();
    requested = command;
    rt_hw_interrupt_enable(level);
    iw_gui_wake(IW_GUI_WAKE_STATE);
}

static void confirm(uint16_t action, void *context)
{
    (void)action; (void)context;
    iw_component_view_t model = {.state = IW_WAITING, .title = demo_texts[3]};
    (void)iw_component_update(&button, &model);
    rt_kprintf("component demo action=confirm state=waiting\n");
}

static iw_component_result_t item(iw_component_t *handle, iw_component_kind_t kind, int16_t y, int16_t height,
    iw_theme_quality_t quality, iw_theme_font_role_t role, iw_component_state_t state,
    const char *title, const char *detail, const char *value)
{
    iw_component_config_t config = {.y = y, .width = 354, .height = height, .quality = quality,
        .font_role = role, .on_action = kind == IW_PILL_BUTTON ? confirm : NULL};
    iw_component_view_t model = {state, title, detail, value};
    return iw_component_create(handle, iw_screen_frame_content(&frame), kind, &config, &model);
}

static iw_component_result_t open_demo(int command)
{
    iw_theme_quality_t quality = command == DEMO_Q0 ? IW_THEME_Q0 : IW_THEME_Q1;
    iw_component_result_t result = iw_screen_frame_create(&frame, lv_layer_top(), IW_FRAME_SCROLL, demo_texts[0], NULL, NULL);
    if (result != IW_COMPONENT_OK) return result;
    if (command >= DEMO_64) {
        iw_theme_font_role_t role = (iw_theme_font_role_t)(IW_THEME_FONT_NUMBER_SMALL + command - DEMO_64);
        result = item(&panel, IW_STATE_PANEL, 0, 240, quality, role, IW_EMPTY, "12:45", demo_texts[7], NULL);
    }
    else {
        result = item(&row, IW_LIST_ROW, 0, 110, quality, IW_THEME_FONT_BODY, IW_NORMAL, demo_texts[1], demo_texts[2], "80%");
        if (result == IW_COMPONENT_OK) result = item(&button, IW_PILL_BUTTON, 118, 60, quality, IW_THEME_FONT_BODY, IW_NORMAL, demo_texts[3], NULL, NULL);
        if (result == IW_COMPONENT_OK) result = item(&panel, IW_STATE_PANEL, 186, 110, quality, IW_THEME_FONT_BODY, IW_EMPTY, demo_texts[4], demo_texts[5], NULL);
        if (result == IW_COMPONENT_OK) result = item(&banner, IW_STATUS_BANNER, 304, 64, quality, IW_THEME_FONT_CAPTION, IW_SUCCESS, demo_texts[6], NULL, NULL);
    }
    if (result != IW_COMPONENT_OK) (void)iw_component_destroy(&frame);
    return result;
}

bool iw_components_demo_process(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    int command = requested;
    rt_hw_interrupt_enable(level);
    if (command == DEMO_IDLE) return false;
    if (!iw_font_port_render_idle() || iw_gui_fault_pending()) return true;
    level = rt_hw_interrupt_disable();
    command = requested;
    requested = DEMO_IDLE;
    rt_hw_interrupt_enable(level);
    if (command == DEMO_FAULT) {
        /* 诊断锁存用于验证退出路径，不伪装成实际堆耗尽或硬件故障。 */
        iw_gui_fault_raise();
        rt_kprintf("component demo injected=owner_cleanup_only\n");
        return true;
    }
    iw_gui_fault_dismiss();
    (void)iw_component_destroy(&frame);
    if (command == DEMO_CLOSE) { rt_kprintf("component demo closed\n"); return false; }
    iw_component_result_t result = open_demo(command);
    if (result != IW_COMPONENT_OK) iw_gui_fault_raise();
    rt_kprintf("component demo mode=%d result=%d\n", command, (int)result);
    return iw_gui_fault_pending();
}

bool iw_components_demo_home(void)
{
    if (!frame.object) return false;
    request(DEMO_CLOSE);
    return true;
}

static void iw_demo(int argc, char **argv)
{
    if (argc != 2 || argv[1][0] < '0' || argv[1][0] > '6' || argv[1][1]) {
        rt_kprintf("iw_demo 0=close 1=Q0 2=Q1 3=64px 4=80px 5=96px 6=cleanup-test\n");
        return;
    }
    request(argv[1][0] - '0');
}
MSH_CMD_EXPORT(iw_demo, Component diagnostics on GUI owner);
