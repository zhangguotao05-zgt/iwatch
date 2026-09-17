#include "iw_components.h"
#include "iw_components_demo.h"
#include "iw_components_demo_text.h"
#include "iw_font.h"
#include "iw_font_port.h"
#include "iw_gui_owner.h"
#include <assert.h>
#include <stdio.h>

extern void test_font_owner(bool owner, bool idle);
extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
extern unsigned test_font_assert_count(void);

/* 仅替换IRQ和唤醒边界；邮箱、组件构造、Home处理及GUI排空逻辑均来自生产文件。 */
typedef int rt_base_t;
static unsigned irq_depth, wakes, submissions;
static bool home_during_frame;
static rt_base_t rt_hw_interrupt_disable(void) { return (int)irq_depth++; }
static void rt_hw_interrupt_enable(rt_base_t saved) { assert(irq_depth == (unsigned)saved + 1u); irq_depth--; }
#define IW_GUI_WAKE_STATE 2u
static void iw_gui_wake(uint32_t reason) { assert(!irq_depth && reason == IW_GUI_WAKE_STATE); wakes++; }
#define rt_kprintf(...) ((void)0)
#define MSH_CMD_EXPORT(function, description)
#include "component_demo_under_test.inc"

static bool app_clock_main_process_font_fault(void) { return iw_gui_fault_process(); }
static uint32_t simulated_frame(void)
{
    submissions++;
    test_font_owner(true, false);
    if (home_during_frame) {
        home_during_frame = false;
        assert(iw_components_demo_home());
    }
    return 5u;
}
#define lv_timer_handler simulated_frame
#include "gui_frame_under_test.inc"
#undef lv_timer_handler

int test_component_navigation(size_t loops)
{
    /* 顶层属性在页面之间由LVGL持有，预热后再取零增长基线。 */
    lv_obj_t *warmup = lv_obj_create(lv_layer_top());
    lv_obj_delete(warmup);
    const size_t bytes = test_font_live_bytes(), blocks = test_font_live_blocks();
    char *open_args[] = {"iw_demo", "2"};
    char *close_args[] = {"iw_demo", "0"};
    for (size_t loop = 0; loop < loops; loop++) {
        test_font_owner(true, true);
        iw_demo(2, open_args);
        assert(gui_process_frame() == 5u && frame.object);
        const unsigned before = submissions;
        iw_demo(2, close_args);
        /* 旧代码会先发新帧，导致每一轮检查都忙，关闭请求一直无法消费。 */
        for (unsigned tick = 0; tick < 100; tick++) {
            assert(gui_process_frame() == 1u && frame.object);
            assert(submissions == before);
        }
        test_font_owner(true, true);
        assert(gui_process_frame() == 5u && !frame.object);
        assert(submissions == before + 1u);
        test_font_owner(true, true);
        assert(iw_font_collect());

        iw_demo(2, open_args);
        assert(gui_process_frame() == 5u && frame.object);
        test_font_owner(true, true);
        home_during_frame = true;
        assert(gui_process_frame() == 1u && frame.object);
        const unsigned after_home = submissions;
        assert(gui_process_frame() == 1u && submissions == after_home);
        test_font_owner(true, true);
        assert(gui_process_frame() == 5u && !frame.object);
        assert(!iw_components_demo_home());
        test_font_owner(true, true);
        assert(iw_font_collect() && !iw_gui_fault_pending());
        assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
    }
    assert(!irq_depth && !test_font_assert_count() && wakes == loops * 4u);
    printf("component_navigation loops=%zu blocked_ticks=%zu asserts=0 result=ok\n", loops, loops * 101u);
    return 0;
}
