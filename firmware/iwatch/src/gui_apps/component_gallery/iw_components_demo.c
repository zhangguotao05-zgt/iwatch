#include "iw_components_demo.h"
#include "iw_components.h"
#include "iw_components_demo_text.h"
#include "iw_gui_owner.h"
#include "iw_font_port.h"
#include "iw_gui_port.h"
#include <rtthread.h>
#include <rthw.h>
#include <stdio.h>

enum { DEMO_IDLE = -1, DEMO_CLOSE, DEMO_Q0, DEMO_Q1, DEMO_64, DEMO_80, DEMO_96, DEMO_FAULT };
enum { GALLERY_BASE = 16, GALLERY_PAGES = 8, GALLERY_VARIANTS = 4, GALLERY_ITEMS = 5 };
static volatile int requested = DEMO_IDLE;
static iw_component_t frame, row, button, panel, banner;
static iw_component_t gallery_items[GALLERY_ITEMS];

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

static void gallery_action(uint16_t action, void *context)
{
    iw_component_t *handle = context;
    /* 等待态保留原命中区，后续点击由组件拦截；回调不保留页面外部引用。 */
    iw_component_view_t model = {.state = IW_WAITING, .title = demo_texts[3]};
    iw_component_result_t result = iw_component_update(handle, &model);
    rt_kprintf("component gallery action=%u result=%d state=waiting\n", (unsigned)action, (int)result);
}

static iw_component_result_t open_gallery(unsigned variant)
{
    static const iw_component_state_t row_states[] = {IW_NORMAL, IW_PRESSED, IW_DISABLED};
    static const iw_component_state_t button_states[] = {IW_NORMAL, IW_PRESSED, IW_DISABLED, IW_WAITING, IW_DANGER};
    static const iw_component_state_t panel_states[] = {IW_LOADING, IW_EMPTY, IW_ERROR, IW_UNAVAILABLE};
    static const iw_component_state_t banner_states[] = {IW_INFO, IW_SUCCESS, IW_WARNING, IW_DANGER};
    const unsigned page = variant / GALLERY_VARIANTS;
    const iw_theme_t *theme = iw_theme_get();
    iw_component_config_t config = {.width = (int16_t)(theme->width - 2 * theme->inset_x),
        .quality = (variant & 2u) ? IW_THEME_Q1 : IW_THEME_Q0,
        .reduced_motion = (variant & 1u) != 0, .font_role = IW_THEME_FONT_BODY};
    char title[64];
    (void)snprintf(title, sizeof(title), "%s %u Q%u M%u", demo_texts[0], page,
        (unsigned)config.quality, config.reduced_motion ? 1u : 0u);
    /* 大字页同时覆盖三种页面框架，其余页面允许滚动完整查看状态。 */
    iw_frame_mode_t mode = page == 5 ? IW_FRAME_NORMAL : page == 6 ? IW_FRAME_FULLSCREEN : IW_FRAME_SCROLL;
    iw_component_result_t result = iw_screen_frame_create(&frame, lv_layer_top(), mode, title, NULL, NULL);
    if (result != IW_COMPONENT_OK) return result;
    const unsigned count = page == 0 ? 3u : page == 1 ? 5u : page < 4 ? 4u : page == 4 ? 3u : 1u;
    for (unsigned i = 0; i < count; i++) {
        iw_component_kind_t kind = IW_STATE_PANEL;
        iw_component_view_t model = {.state = IW_EMPTY};
        config.height = 110;
        if (page == 0) {
            kind = IW_LIST_ROW; model.state = row_states[i]; model.detail = demo_texts[2]; model.value = "80%";
        }
        else if (page == 1) {
            kind = IW_PILL_BUTTON; model.state = button_states[i]; config.height = (int16_t)theme->button_height;
            config.on_action = gallery_action; config.action = (uint16_t)i; config.context = &gallery_items[i];
        }
        else if (page == 2) { model.state = panel_states[i]; model.detail = demo_texts[5]; }
        else if (page == 3) {
            kind = IW_STATUS_BANNER; model.state = banner_states[i]; model.detail = demo_texts[7];
        }
        else if (page == 4) {
            /* 完整正文给足多行高度；长标题、右值另页内展示裁剪边界。 */
            model.title = i == 0 ? demo_texts[LONG_TITLE] : i == 1 ? demo_texts[1] : demo_texts[LONG_TITLE];
            model.detail = i == 0 ? demo_texts[LONG_DETAIL] : NULL;
            config.height = i == 0 ? 410 : 80;
            if (i == 1) { kind = IW_LIST_ROW; model.state = IW_NORMAL; model.value = demo_texts[LONG_VALUE]; }
        }
        else {
            model.title = "12:45"; model.detail = demo_texts[7]; config.height = 240;
            config.font_role = (iw_theme_font_role_t)(IW_THEME_FONT_NUMBER_SMALL + page - 5);
        }
        if (!model.title) model.title = demo_texts[STATE_TEXT_BASE + model.state];
        result = iw_component_create(&gallery_items[i], iw_screen_frame_content(&frame), kind, &config, &model);
        if (result != IW_COMPONENT_OK) { (void)iw_component_destroy(&frame); return result; }
        config.y += config.height + (int16_t)theme->gap[1];
    }
    rt_kprintf("component gallery page=%u quality=%u reduced=%u items=%u\n", page,
        (unsigned)config.quality, config.reduced_motion ? 1u : 0u, count);
    return IW_COMPONENT_OK;
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
    iw_component_result_t result = command >= GALLERY_BASE ? open_gallery((unsigned)(command - GALLERY_BASE)) : open_demo(command);
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

static void iw_gallery(int argc, char **argv)
{
    /* 固定范围的三位参数，拒绝负数、溢出和多余尾字符，串口线程只投递邮箱。 */
    if (argc != 4 || argv[1][0] < '0' || argv[1][0] >= '0' + GALLERY_PAGES || argv[1][1] ||
        argv[2][0] < '0' || argv[2][0] > '1' || argv[2][1] ||
        argv[3][0] < '0' || argv[3][0] > '1' || argv[3][1]) {
        rt_kprintf("iw_gallery <page:0-7> <quality:0-1> <reduced:0-1>; iw_demo 0=close 6=cleanup-test\n");
        return;
    }
    request(GALLERY_BASE + (argv[1][0] - '0') * GALLERY_VARIANTS + (argv[2][0] - '0') * 2 + argv[3][0] - '0');
}
MSH_CMD_EXPORT(iw_gallery, Component state and motion gallery);
