#include "iw_components.h"
#include "iw_components_demo.h"
#include "iw_components_demo_text.h"
#include "iw_font.h"
#include "iw_font_port.h"
#include "iw_gui_owner.h"
#include "src/draw/lv_draw_private.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void test_font_owner(bool owner, bool idle);
extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
extern unsigned test_font_assert_count(void);
extern void test_font_arm_failure(size_t index);
extern void test_component_capture(lv_display_t *display, lv_obj_t *content, unsigned variant);
extern int test_epic_glyph(lv_font_glyph_dsc_t *glyph);
extern unsigned test_epic_submissions(void);

/* 仅替换IRQ和唤醒边界；邮箱、组件构造、Home处理及GUI排空逻辑均来自生产文件。 */
typedef int rt_base_t;
static unsigned irq_depth, wakes, submissions, gallery_callbacks;
static bool home_during_frame;
static rt_base_t rt_hw_interrupt_disable(void) { return (int)irq_depth++; }
static void rt_hw_interrupt_enable(rt_base_t saved) { assert(irq_depth == (unsigned)saved + 1u); irq_depth--; }
#define IW_GUI_WAKE_STATE 2u
static void iw_gui_wake(uint32_t reason) { assert(!irq_depth && reason == IW_GUI_WAKE_STATE); wakes++; }
static int rt_kprintf(const char *format, ...)
{
    if (!strncmp(format, "component gallery action=", 25)) gallery_callbacks++;
    return 0;
}
#define MSH_CMD_EXPORT(function, description)
#include "component_demo_under_test.inc"

static bool app_clock_main_process_font_fault(void) { return iw_gui_fault_process(); }
/* 路由与真实框架的集成另由 D08 入口测试覆盖，本组只测原展示页调度。 */
static bool iw_router_process(void) { return false; }
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

static void gallery_open(unsigned variant)
{
    char page[] = {(char)('0' + variant / 4), 0};
    char quality[] = {(char)('0' + (variant / 2) % 2), 0};
    char motion[] = {(char)('0' + variant % 2), 0};
    char *args[] = {"iw_gallery", page, quality, motion};
    iw_gallery(4, args);
    assert(!iw_components_demo_process() && frame.object);
}

static void gallery_close(void)
{
    assert(iw_components_demo_home());
    assert(!iw_components_demo_process() && !frame.object);
    assert(!iw_components_demo_home() && iw_font_collect());
    for (unsigned i = 0; i < GALLERY_ITEMS; i++) assert(!gallery_items[i].object);
}

static void invalid_gallery_commands(void)
{
    const char *bad[] = {"", "-1", "8", "10", "1x", "2147483648"};
    for (unsigned argument = 1; argument <= 3; argument++) {
        for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            char *args[] = {"iw_gallery", "0", "0", "0"};
            args[argument] = (char *)bad[i];
            iw_gallery(4, args);
            assert(requested == DEMO_IDLE && !frame.object);
        }
    }
    char *args[] = {"iw_gallery", "0", "0", "0"};
    iw_gallery(3, args); iw_gallery(5, args);
    assert(requested == DEMO_IDLE && !irq_depth);
}

int test_component_gallery(size_t loops)
{
    lv_obj_t *warmup = lv_obj_create(lv_layer_top());
    lv_obj_delete(warmup);
    const size_t bytes = test_font_live_bytes(), blocks = test_font_live_blocks();
    assert(strlen(demo_texts[LONG_TITLE]) == 127 && strlen(demo_texts[LONG_DETAIL]) == 255 &&
        strlen(demo_texts[LONG_VALUE]) == 47);
    invalid_gallery_commands();
    unsigned metadata_oom = 0, epic_oom = 0;
    for (size_t loop = 0; loop < loops; loop++) {
        const unsigned variant = (unsigned)(loop % 32);
        test_font_owner(true, true);
        gallery_open(variant);
        if (variant / 4 == 1) {
            /* 使用实际按钮回调进入等待，绘制任务必须显示省略号；重复点击无分配。 */
            lv_obj_update_layout(frame.object);
            unsigned callbacks_before = gallery_callbacks;
            assert(lv_obj_send_event(gallery_items[0].object, LV_EVENT_CLICKED, NULL) == LV_RESULT_OK);
            assert(lv_obj_send_event(gallery_items[0].object, LV_EVENT_CLICKED, NULL) == LV_RESULT_OK);
            assert(gallery_callbacks == callbacks_before + 1);
            lv_layer_t layer = {0};
            layer._clip_area = (lv_area_t){0, 0, 389, 449}; layer.buf_area = layer._clip_area; layer.opa = 255;
            assert(lv_obj_send_event(gallery_items[0].object, LV_EVENT_DRAW_MAIN, &layer) == LV_RESULT_OK);
            unsigned labels = 0;
            for (lv_draw_task_t *task = layer.draw_task_head; task; task = task->next) {
                if (task->type == LV_DRAW_TASK_TYPE_LABEL) {
                    assert(!strcmp(((lv_draw_label_dsc_t *)task->draw_dsc)->text, "...")); labels++;
                }
                task->state = LV_DRAW_TASK_STATE_FINISHED;
            }
            assert(labels == 1);
            (void)lv_draw_dispatch_layer(lv_display_get_default(), &layer);
            assert(!layer.draw_task_head);
        }
        if ((loop + loop / 32) & 1u) {
            iw_component_t other = {0};
            assert(iw_screen_frame_create(&other, lv_layer_top(), IW_FRAME_NORMAL, "Other", NULL, NULL) == IW_COMPONENT_OK);
            /* 真实字形分配失败时同时保留展示页、第二页面和一个待处理关闭请求。 */
            iw_font_ref_t ref = {0}; lv_font_glyph_dsc_t glyph = {0};
            assert(iw_font_acquire(IW_FONT_18, &ref) == IW_FONT_OK);
            if ((loop / 32) & 1u) {
                assert(lv_font_get_glyph_dsc(ref.font, &glyph, 0x601d, 0));
                unsigned submitted = test_epic_submissions();
                test_font_arm_failure(1);
                assert(!test_epic_glyph(&glyph) && test_epic_submissions() == submitted);
                epic_oom++;
            }
            else {
                test_font_arm_failure(1);
                assert(!lv_font_get_glyph_dsc(ref.font, &glyph, 0x601d, 0));
                metadata_oom++;
            }
            test_font_arm_failure(0);
            assert(iw_font_release(&ref) == IW_FONT_OK && iw_font_fault_pending());
            test_font_owner(true, false);
            assert(iw_components_demo_home());
            assert(iw_gui_fault_process() && iw_components_demo_process() && frame.object && other.object);
            test_font_owner(true, true);
            assert(!iw_gui_fault_process() && !frame.object && !other.object);
            assert(!iw_components_demo_process() && !iw_gui_fault_pending());
            assert(iw_component_destroy(&other) == IW_COMPONENT_OK);
            gallery_open(variant);
        }
        gallery_close();
        assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
    }
    assert(!irq_depth && !test_font_assert_count());
    printf("component_gallery loops=%zu profiles=32 metadata_oom=%u epic_oom=%u asserts=0 result=ok\n", loops, metadata_oom, epic_oom);
    return 0;
}

int test_component_gallery_render(lv_display_t *display, unsigned variant)
{
    assert(variant < 32);
    gallery_open(variant);
    test_component_capture(display, iw_screen_frame_content(&frame), variant);
    gallery_close();
    printf("component_gallery_render=%u asserts=0 result=ok\n", variant);
    return 0;
}
