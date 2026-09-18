#include "iw_components.h"
#include "iw_font.h"
#include "iw_gui_owner.h"
#include "iw_components_demo_text.h"
#include "src/draw/lv_draw_private.h"
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_class_private.h"
#include "src/draw/sw/lv_draw_sw.h"
#include "src/draw/sw/lv_draw_sw_mask_private.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void test_font_arm_failure(size_t index);
extern void test_font_fail_forever(void);
extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
extern size_t test_font_allocation_sequence(void);
extern unsigned test_font_assert_count(void);
extern void test_font_owner(bool owner, bool idle);
extern int test_component_navigation(size_t loops);
extern int test_component_gallery(size_t loops);
extern int test_component_gallery_render(lv_display_t *display, unsigned variant);
extern int test_touch_input(size_t loops);
extern int test_router(lv_display_t *display, size_t number, bool failure);
extern int test_sdk_navigation(void);
extern int test_product_view(lv_display_t *display, size_t number, unsigned mode);
extern int test_product_controller(size_t loops);
extern int test_product_router(lv_display_t *display,size_t loops);

static unsigned cancellations, recoveries, quiesced, actions;
static bool recovery_visible;
void iw_gui_cancel_input(void) { cancellations++; }
void iw_recovery_show(const char *owner) { assert(!strcmp(owner, "gui")); recoveries++; recovery_visible = true; }
void iw_recovery_hide(const char *owner) { assert(!strcmp(owner, "gui")); recovery_visible = false; }
static void quiesce(void *context) { assert(context == &quiesced); quiesced++; }
static void action(uint16_t id, void *context) { assert(id == 42 && context == &actions); actions++; }

static void unused_stop(void *context) { (void)context; }

static void owner_boundaries(void)
{
    iw_gui_owner_t owners[IW_GUI_OWNER_LIMIT + 1] = {0};
    for (unsigned i = 0; i < IW_GUI_OWNER_LIMIT; i++) assert(iw_gui_owner_add(&owners[i], unused_stop, NULL));
    assert(!iw_gui_owner_add(&owners[IW_GUI_OWNER_LIMIT], unused_stop, NULL));
    assert(!iw_gui_owner_add(&owners[0], unused_stop, NULL));
    test_font_owner(false, true);
    assert(!iw_gui_owner_remove(&owners[0]) && !iw_gui_owner_add(&owners[IW_GUI_OWNER_LIMIT], unused_stop, NULL));
    test_font_owner(true, true);
    for (unsigned i = 0; i < IW_GUI_OWNER_LIMIT; i++) {
        assert(iw_gui_owner_remove(&owners[i]));
        assert(iw_gui_owner_remove(&owners[i]));
    }
    size_t blocks = test_font_live_blocks(), bytes = test_font_live_bytes();
    /* 覆盖尚无 spec_attr 的父对象，以及16位 child_cnt 的失败无副作用边界。 */
    for (unsigned point = 1; point <= 3; point++) {
        lv_obj_t parent = {.class_p = &lv_obj_class};
        test_font_arm_failure(point);
        assert(!lv_obj_class_create_obj_checked(&lv_obj_class, &parent));
        test_font_arm_failure(0);
        assert(!parent.spec_attr);
    }
    lv_obj_spec_attr_t attr = {.child_cnt = UINT16_MAX};
    lv_obj_t parent = {.class_p = &lv_obj_class, .spec_attr = &attr};
    assert(!lv_obj_class_create_obj_checked(&lv_obj_class, &parent));
    assert(attr.child_cnt == UINT16_MAX && !attr.children);
    assert(test_font_live_blocks() == blocks && test_font_live_bytes() == bytes);
}

static iw_component_config_t config(void)
{
    return (iw_component_config_t){.width = 354, .height = 120, .quality = IW_THEME_Q1,
        .font_role = IW_THEME_FONT_BODY, .action = 42, .on_action = action, .context = &actions};
}

static iw_component_view_t view(iw_component_kind_t kind)
{
    return (iw_component_view_t){.state = kind == IW_STATE_PANEL ? IW_EMPTY : kind == IW_STATUS_BANNER ? IW_INFO : IW_NORMAL,
        .title = "Hello", .detail = kind == IW_PILL_BUTTON ? "" : "World", .value = kind == IW_LIST_ROW ? "80%" : ""};
}

static iw_component_result_t make(iw_component_t *handle, lv_obj_t *parent, iw_component_kind_t kind)
{
    iw_component_config_t options = config();
    if (kind == IW_PILL_BUTTON) options.height = 60;
    iw_component_view_t model = view(kind);
    if (kind == IW_SCREEN_FRAME) return iw_screen_frame_create(handle, parent, IW_FRAME_SCROLL, "Title", quiesce, &quiesced);
    return iw_component_create(handle, parent, kind, &options, &model);
}

/* 使用真实 LVGL 建立绘制任务；此处模拟异步提交，不把任务内容检查当成屏幕像素验收。 */
static void paint(lv_obj_t *object, lv_layer_t *layer)
{
    memset(layer, 0, sizeof(*layer));
    layer->_clip_area = (lv_area_t){0, 0, 389, 449};
    layer->buf_area = layer->_clip_area;
    layer->opa = 255;
    assert(lv_obj_send_event(object, LV_EVENT_DRAW_MAIN, layer) == LV_RESULT_OK);
}

static void finish_tasks(lv_display_t *display, lv_layer_t *layer)
{
    for (lv_draw_task_t *task = layer->draw_task_head; task; task = task->next) task->state = LV_DRAW_TASK_STATE_FINISHED;
    (void)lv_draw_dispatch_layer(display, layer);
    assert(!layer->draw_task_head);
}

static void require_baseline(size_t blocks, size_t bytes)
{
    assert(iw_font_collect());
    if (test_font_live_blocks() != blocks || test_font_live_bytes() != bytes) {
        printf("memory expected=%zu/%zu actual=%zu/%zu\n", blocks, bytes, test_font_live_blocks(), test_font_live_bytes());
        fflush(stdout);
    }
    assert(test_font_live_blocks() == blocks && test_font_live_bytes() == bytes);
    assert(test_font_assert_count() == 0);
}

static void state_matrix(lv_display_t *display)
{
    iw_component_t frame = {0}, item = {0};
    assert(make(&frame, lv_screen_active(), IW_SCREEN_FRAME) == IW_COMPONENT_OK);
    for (unsigned profile = 0; profile < 4; profile++) {
        for (unsigned kind = IW_LIST_ROW; kind < IW_COMPONENT_KIND_COUNT; kind++) {
            iw_component_config_t options = config();
            options.quality = (iw_theme_quality_t)(profile / 2);
            options.reduced_motion = (profile % 2) != 0;
            if (kind == IW_PILL_BUTTON) options.height = 60;
            iw_component_view_t initial = view((iw_component_kind_t)kind);
            assert(iw_component_create(&item, iw_screen_frame_content(&frame), (iw_component_kind_t)kind, &options, &initial) == IW_COMPONENT_OK);
            lv_obj_update_layout(item.object);
            int32_t width = lv_obj_get_width(item.object), height = lv_obj_get_height(item.object);
            for (unsigned state = 0; state < IW_COMPONENT_STATE_COUNT; state++) {
                iw_component_view_t model = view((iw_component_kind_t)kind);
                model.state = (iw_component_state_t)state;
                bool valid = kind == IW_LIST_ROW ? state <= IW_DISABLED :
                    kind == IW_PILL_BUTTON ? state <= IW_DANGER :
                    kind == IW_STATE_PANEL ? state >= IW_LOADING && state <= IW_UNAVAILABLE :
                    state == IW_DANGER || state >= IW_INFO;
                size_t before = test_font_allocation_sequence();
                assert(iw_component_update(&item, &model) == (valid ? IW_COMPONENT_OK : IW_COMPONENT_INVALID));
                assert(before == test_font_allocation_sequence());
                assert(lv_obj_get_width(item.object) == width && lv_obj_get_height(item.object) == height);
                if (valid) {
                    unsigned previous = actions;
                    assert(lv_obj_send_event(item.object, LV_EVENT_CLICKED, NULL) == LV_RESULT_OK);
                    bool expected_action = kind <= IW_PILL_BUTTON && state != IW_DISABLED && state != IW_WAITING;
                    assert(actions == previous + (expected_action ? 1u : 0u));
                    lv_layer_t layer; paint(item.object, &layer); finish_tasks(display, &layer);
                    if (expected_action) {
                        assert(lv_obj_send_event(item.object, LV_EVENT_PRESS_LOST, NULL) == LV_RESULT_OK);
                        lv_tick_inc(80); assert(!iw_components_tick());
                        assert(lv_obj_send_event(item.object, LV_EVENT_PRESSED, NULL) == LV_RESULT_OK);
                        assert(iw_components_tick() == !options.reduced_motion);
                        lv_tick_inc(80); assert(!iw_components_tick());
                        paint(item.object, &layer);
                        assert(layer.draw_task_head);
                        /* 只缩绘制区域；正常/减弱动效的点击区域必须完全一致。 */
                        assert(lv_area_get_width(&layer.draw_task_head->area) == width - (options.reduced_motion ? 0 : 10));
                        assert(lv_obj_get_width(item.object) == width && lv_obj_get_height(item.object) == height);
                        finish_tasks(display, &layer);
                        /* 输入取消必须同时还原 LVGL 按下状态和组件自己的缩放动效。 */
                        assert(lv_obj_send_event(item.object, LV_EVENT_INDEV_RESET, NULL) == LV_RESULT_OK);
                        lv_tick_inc(80); assert(!iw_components_tick());
                        assert(!lv_obj_has_state(item.object, LV_STATE_PRESSED));
                        paint(item.object, &layer);
                        assert(layer.draw_task_head && lv_area_get_width(&layer.draw_task_head->area) == width);
                        finish_tasks(display, &layer);
                    }
                }
            }
            test_font_owner(false, true);
            assert(iw_component_destroy(&item) == IW_COMPONENT_BUSY && !iw_screen_frame_content(&frame));
            test_font_owner(true, false);
            assert(iw_component_destroy(&item) == IW_COMPONENT_BUSY && item.object);
            test_font_owner(true, true);
            assert(iw_component_destroy(&item) == IW_COMPONENT_OK);
        }
    }
    iw_component_config_t options = config(); options.height = 60;
    iw_component_view_t model = view(IW_LIST_ROW);
    assert(iw_component_create(&item, iw_screen_frame_content(&frame), IW_LIST_ROW, &options, &model) == IW_COMPONENT_INVALID);
    assert(!item.object);
    assert(iw_component_destroy(&frame) == IW_COMPONENT_OK && iw_font_collect());
    actions = 0;
}

static void text_boundaries(lv_display_t *display)
{
    iw_component_t frame = {0}, item = {0};
    assert(make(&frame, lv_screen_active(), IW_SCREEN_FRAME) == IW_COMPONENT_OK);
    iw_component_config_t options = config(); options.height = 410;
    iw_component_view_t model = {IW_NORMAL, demo_texts[LONG_TITLE], demo_texts[LONG_DETAIL], demo_texts[LONG_VALUE]};
    assert(iw_component_create(&item, iw_screen_frame_content(&frame), IW_LIST_ROW, &options, &model) == IW_COMPONENT_OK);
    lv_obj_update_layout(frame.object);
    for (unsigned field = 0; field < 3; field++) {
        const char *original = field == 0 ? model.title : field == 1 ? model.detail : model.value;
        char excess[258];
        size_t length = strlen(original);
        memcpy(excess, original, length); excess[length] = 'X'; excess[length + 1] = 0;
        iw_component_view_t invalid = model;
        if (field == 0) invalid.title = excess;
        else if (field == 1) invalid.detail = excess;
        else invalid.value = excess;
        size_t before = test_font_allocation_sequence();
        assert(iw_component_update(&item, &invalid) == IW_COMPONENT_INVALID);
        assert(test_font_allocation_sequence() == before);
        /* 非法更新保持三个字段，已提交绘制的字符串也不能被后续更新改写。 */
        lv_layer_t layer; paint(item.object, &layer);
        iw_component_view_t next = view(IW_LIST_ROW);
        assert(iw_component_update(&item, &next) == IW_COMPONENT_OK);
        unsigned labels = 0;
        for (lv_draw_task_t *task = layer.draw_task_head; task; task = task->next) {
            if (task->type != LV_DRAW_TASK_TYPE_LABEL) continue;
            const char *text = ((lv_draw_label_dsc_t *)task->draw_dsc)->text;
            assert(!strcmp(text, model.title) || !strcmp(text, model.detail) || !strcmp(text, model.value));
            assert(task->clip_area.x1 >= item.object->coords.x1 && task->clip_area.x2 <= item.object->coords.x2);
            labels++;
        }
        assert(labels == 3);
        finish_tasks(display, &layer);
        assert(iw_component_update(&item, &model) == IW_COMPONENT_OK);
    }
    assert(iw_component_destroy(&frame) == IW_COMPONENT_OK && iw_font_collect());
}

static int failure_case(lv_display_t *display, iw_component_kind_t kind, bool draw, size_t point)
{
    size_t blocks = test_font_live_blocks(), bytes = test_font_live_bytes();
    iw_component_t frame = {0}, item = {0};
    lv_obj_t *parent = lv_display_get_screen_active(display);
    if (kind != IW_SCREEN_FRAME) {
        assert(make(&frame, parent, IW_SCREEN_FRAME) == IW_COMPONENT_OK);
        parent = iw_screen_frame_content(&frame);
    }
    if (draw) assert(make(&item, parent, kind) == IW_COMPONENT_OK);
    lv_obj_update_layout(lv_display_get_screen_active(display));
    size_t begin = test_font_allocation_sequence();
    test_font_arm_failure(point);
    iw_component_result_t result = IW_COMPONENT_OK;
    lv_layer_t layer;
    if (draw) paint(item.object, &layer);
    else result = make(&item, parent, kind);
    test_font_arm_failure(0);
    size_t allocations = test_font_allocation_sequence() - begin;
    if (point && draw) assert(iw_gui_fault_pending());
    if (point && !draw) assert(result != IW_COMPONENT_OK && !item.object);
    if (draw) {
        test_font_owner(true, false);
        if (point) {
            assert(iw_gui_fault_process());
            assert(item.object && recoveries == 0);
        }
        finish_tasks(display, &layer);
        test_font_owner(true, true);
    }
    if (iw_gui_fault_pending()) assert(!iw_gui_fault_process());
    assert(iw_component_destroy(&item) == IW_COMPONENT_OK);
    assert(iw_component_destroy(&frame) == IW_COMPONENT_OK);
    assert(iw_component_destroy(&item) == IW_COMPONENT_OK);
    iw_gui_fault_dismiss();
    require_baseline(blocks, bytes);
    printf("component=%u draw=%u allocations=%zu point=%zu asserts=0 result=ok\n", (unsigned)kind, draw, allocations, point);
    return 0;
}

static void input_and_snapshot(lv_display_t *display, iw_component_t *item)
{
    iw_component_view_t model = view(IW_PILL_BUTTON);
    model.state = IW_WAITING;
    size_t before = test_font_allocation_sequence();
    assert(iw_component_update(item, &model) == IW_COMPONENT_OK);
    assert(before == test_font_allocation_sequence());
    assert(lv_obj_send_event(item->object, LV_EVENT_CLICKED, NULL) == LV_RESULT_OK && actions == 0);
    model.state = IW_DISABLED;
    assert(iw_component_update(item, &model) == IW_COMPONENT_OK);
    assert(lv_obj_send_event(item->object, LV_EVENT_CLICKED, NULL) == LV_RESULT_OK && actions == 0);
    model.state = IW_NORMAL;
    char title[] = "Before";
    model.title = title;
    assert(iw_component_update(item, &model) == IW_COMPONENT_OK);
    title[0] = 'X';
    lv_layer_t layer;
    paint(item->object, &layer);
    model.title = "After";
    assert(iw_component_update(item, &model) == IW_COMPONENT_OK);
    unsigned labels = 0;
    for (lv_draw_task_t *task = layer.draw_task_head; task; task = task->next) {
        if (task->type == LV_DRAW_TASK_TYPE_LABEL) {
            assert(!strcmp(((lv_draw_label_dsc_t *)task->draw_dsc)->text, "Before"));
            labels++;
        }
    }
    assert(labels == 1);
    finish_tasks(display, &layer);
    assert(lv_obj_send_event(item->object, LV_EVENT_PRESSED, NULL) == LV_RESULT_OK);
    lv_tick_inc(40); assert(iw_components_tick());
    lv_tick_inc(40); assert(!iw_components_tick());
    assert(lv_obj_send_event(item->object, LV_EVENT_PRESS_LOST, NULL) == LV_RESULT_OK && actions == 0);
    assert(lv_obj_send_event(item->object, LV_EVENT_CLICKED, NULL) == LV_RESULT_OK && actions == 1);
    model.title = "\xed\xa0\x80";
    assert(iw_component_update(item, &model) == IW_COMPONENT_INVALID);
    char long_text[129]; memset(long_text, 'A', sizeof(long_text)); long_text[128] = 0;
    model.title = long_text;
    assert(iw_component_update(item, &model) == IW_COMPONENT_INVALID);
}

static int lifecycle(lv_display_t *display, size_t repeat)
{
    size_t blocks = test_font_live_blocks(), bytes = test_font_live_bytes();
    owner_boundaries();
    state_matrix(display);
    text_boundaries(display);
    require_baseline(blocks, bytes);
    for (size_t cycle = 0; cycle < repeat; cycle++) {
        iw_component_t frame = {0}, items[4] = {0}, other = {0}, other_item = {0};
        assert(make(&frame, lv_display_get_screen_active(display), IW_SCREEN_FRAME) == IW_COMPONENT_OK);
        for (unsigned i = 0; i < 4; i++) assert(make(&items[i], iw_screen_frame_content(&frame), (iw_component_kind_t)(i + 1)) == IW_COMPONENT_OK);
        lv_obj_update_layout(frame.object);
        assert(lv_obj_get_width(iw_screen_frame_content(&frame)) == 354);
        if (!cycle) input_and_snapshot(display, &items[1]);
        assert(make(&other, lv_display_get_screen_active(display), IW_SCREEN_FRAME) == IW_COMPONENT_OK);
        assert(make(&other_item, iw_screen_frame_content(&other), IW_LIST_ROW) == IW_COMPONENT_OK);
        unsigned before = quiesced;
        if (cycle & 1u) {
            /* 从真实 TinyTTF 分配入口锁存故障，其他页面仍持有共享字体引用。 */
            iw_font_ref_t ref = {0};
            lv_font_glyph_dsc_t glyph = {0};
            assert(iw_font_acquire(IW_FONT_26, &ref) == IW_FONT_OK);
            test_font_arm_failure(1);
            assert(!lv_font_get_glyph_dsc(ref.font, &glyph, 0x601d, 0));
            test_font_arm_failure(0);
            assert(iw_font_release(&ref) == IW_FONT_OK && iw_font_fault_pending());
        }
        else iw_gui_fault_raise();
        test_font_owner(true, false);
        assert(iw_gui_fault_process() && frame.object && other.object && quiesced == before);
        test_font_owner(true, true);
        assert(!iw_gui_fault_process() && !frame.object && !other.object && quiesced == before + 2);
        for (unsigned i = 0; i < 4; i++) assert(!items[i].object && iw_component_destroy(&items[i]) == IW_COMPONENT_OK);
        assert(!other_item.object && recovery_visible);
        iw_gui_fault_dismiss(); assert(!recovery_visible);
        assert(!iw_gui_fault_process());
        require_baseline(blocks, bytes);
    }
    /* 真实持续内存耗尽期间回收含多个子节点的页面，缩容失败不能丢失剩余指针。 */
    iw_component_t frame = {0}, items[4] = {0};
    assert(make(&frame, lv_display_get_screen_active(display), IW_SCREEN_FRAME) == IW_COMPONENT_OK);
    for (unsigned i = 0; i < 4; i++) assert(make(&items[i], iw_screen_frame_content(&frame), (iw_component_kind_t)(i + 1)) == IW_COMPONENT_OK);
    test_font_fail_forever();
    assert(iw_component_destroy(&frame) == IW_COMPONENT_OK);
    assert(iw_font_collect());
    test_font_arm_failure(0);
    for (unsigned i = 0; i < 4; i++) assert(!items[i].object);
    require_baseline(blocks, bytes);
    printf("component_cycles=%zu kinds=5 quiesced=%u cancels=%u recovery=%u asserts=0 result=ok\n",
        repeat, quiesced, cancellations, recoveries);
    return 0;
}

static uint8_t render_buffer[390 * 450 * 2];
static unsigned fill_failures;
static void fill_failed(void) { fill_failures++; iw_gui_fault_raise(); }

static int fill_oom_case(bool occupied_cache, size_t point)
{
    lv_draw_sw_mask_cleanup();
    size_t blocks = test_font_live_blocks(), bytes = test_font_live_bytes();
    lv_draw_buf_t *buffer = lv_draw_buf_create(128, 128, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    assert(buffer);
    lv_layer_t layer = {0};
    layer.draw_buf = buffer;
    layer.color_format = LV_COLOR_FORMAT_RGB565;
    layer.buf_area = (lv_area_t){0, 0, 127, 127};
    lv_draw_task_t task = {0};
    task.target_layer = &layer;
    task.clip_area = layer.buf_area;
    lv_draw_fill_dsc_t dsc;
    lv_draw_fill_dsc_init(&dsc);
    dsc.radius = 24;
    dsc.opa = 224;
    dsc.color = lv_color_hex(0x17181c);
    dsc.grad.dir = LV_GRAD_DIR_VER;
    dsc.grad.stops_count = 2;
    dsc.grad.stops[0].color = lv_color_white();
    dsc.grad.stops[1].color = lv_color_black();
    dsc.grad.stops[1].frac = 255;
    dsc.grad.stops[0].opa = dsc.grad.stops[1].opa = 255;
    dsc.allocation_failed_cb = fill_failed;
    lv_draw_sw_mask_radius_param_t held[LV_DRAW_SW_CIRCLE_CACHE_SIZE];
    if (occupied_cache) {
        for (unsigned i = 0; i < LV_DRAW_SW_CIRCLE_CACHE_SIZE; i++)
            lv_draw_sw_mask_radius_init(&held[i], &layer.buf_area, (int32_t)(i + 2), false);
    }
    size_t begin = test_font_allocation_sequence();
    test_font_arm_failure(point);
    /* 实際调用目标同款圆角/渐变软件路径，覆盖遮罩、缓存、临时项和渐变图。 */
    lv_draw_sw_fill(&task, &dsc, &layer.buf_area);
    test_font_arm_failure(0);
    size_t allocations = test_font_allocation_sequence() - begin;
    assert(fill_failures == (point ? 1u : 0u));
    if (occupied_cache) {
        for (unsigned i = 0; i < LV_DRAW_SW_CIRCLE_CACHE_SIZE; i++) lv_draw_sw_mask_free_param(&held[i]);
    }
    lv_draw_buf_destroy(buffer);
    lv_draw_sw_mask_cleanup();
    if (point) assert(!iw_gui_fault_process());
    require_baseline(blocks, bytes);
    printf("component_fill occupied=%u allocations=%zu point=%zu asserts=0 result=ok\n", occupied_cache, allocations, point);
    return 0;
}

static unsigned flushes;
static void render_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area; (void)pixels;
    flushes++;
    lv_display_flush_ready(display);
}

void test_component_capture(lv_display_t *display, lv_obj_t *content, unsigned variant)
{
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, render_buffer, NULL, sizeof(render_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, render_flush);
    for (unsigned bottom = 0; bottom < 2; bottom++) {
        lv_obj_update_layout(content);
        lv_obj_scroll_to_y(content, bottom ? LV_COORD_MAX : 0, LV_ANIM_OFF);
        memset(render_buffer, 0, sizeof(render_buffer));
        unsigned before_flush = flushes;
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        assert(flushes > before_flush && !iw_gui_fault_pending() && !test_font_assert_count());
        char filename[160];
        snprintf(filename, sizeof(filename), "firmware/tests/build/d07-a0/gallery-%02u-%u.rgb565", variant, bottom);
        FILE *file = NULL;
#ifdef _WIN32
        assert(fopen_s(&file, filename, "wb") == 0);
#else
        file = fopen(filename, "wb");
#endif
        assert(file && fwrite(render_buffer, 1, sizeof(render_buffer), file) == sizeof(render_buffer));
        fclose(file);
    }
}

static int render_case(lv_display_t *display, unsigned mode)
{
    iw_component_t frame = {0}, items[4] = {0};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, render_buffer, NULL, sizeof(render_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, render_flush);
    assert(iw_screen_frame_create(&frame, lv_screen_active(), IW_FRAME_SCROLL, demo_texts[0], NULL, NULL) == IW_COMPONENT_OK);
    iw_component_config_t options = config();
    options.quality = mode == 0 ? IW_THEME_Q0 : IW_THEME_Q1;
    if (mode < 2) {
        static const int16_t positions[] = {0, 118, 186, 304};
        static const int16_t heights[] = {110, 60, 110, 64};
        for (unsigned i = 0; i < 4; i++) {
            iw_component_kind_t kind = (iw_component_kind_t)(i + 1);
            iw_component_view_t model = view(kind);
            model.title = demo_texts[i == 0 ? 1 : i == 1 ? 3 : i == 2 ? 4 : 6];
            model.detail = i == 0 ? demo_texts[2] : i == 2 ? demo_texts[5] : NULL;
            if (i == 3) model.state = IW_SUCCESS;
            options.y = positions[i]; options.height = heights[i];
            options.font_role = i == 3 ? IW_THEME_FONT_CAPTION : IW_THEME_FONT_BODY;
            assert(iw_component_create(&items[i], iw_screen_frame_content(&frame), kind, &options, &model) == IW_COMPONENT_OK);
        }
    }
    else {
        options.height = 240;
        options.font_role = (iw_theme_font_role_t)(IW_THEME_FONT_NUMBER_SMALL + mode - 2);
        iw_component_view_t model = {.state = IW_EMPTY, .title = "12:45", .detail = demo_texts[7]};
        assert(iw_component_create(&items[0], iw_screen_frame_content(&frame), IW_STATE_PANEL, &options, &model) == IW_COMPONENT_OK);
    }
    lv_refr_now(display);
    assert(flushes && !iw_gui_fault_pending() && !test_font_assert_count());
    char filename[128];
    snprintf(filename, sizeof(filename), "firmware/tests/build/d07-a0/component-%u.rgb565", mode);
    FILE *file = NULL;
#ifdef _WIN32
    assert(fopen_s(&file, filename, "wb") == 0);
#else
    file = fopen(filename, "wb");
#endif
    assert(file && fwrite(render_buffer, 1, sizeof(render_buffer), file) == sizeof(render_buffer));
    fclose(file);
    assert(iw_component_destroy(&frame) == IW_COMPONENT_OK && iw_font_collect());
    printf("component_render=%u flushes=%u asserts=0 result=ok\n", mode, flushes);
    return 0;
}

int test_components(const void *data, size_t size, const char *stage, size_t number)
{
    assert(iw_font_init(data, (uint32_t)size));
    lv_display_t *display = lv_display_create(390, 450);
    assert(display);
    /* 默认 screen 的延迟属性预热后固定为基线；组件创建失败不得遗留其私有资源。 */
    lv_obj_t *warmup = lv_obj_create(lv_display_get_screen_active(display));
    lv_obj_delete(warmup);
    int result;
    if (!strcmp(stage, "component_product_router")) result = test_product_router(display,number);
    else if (!strcmp(stage, "component_product_controller")) result = test_product_controller(number);
    else if (!strcmp(stage, "component_product_oom")) result = test_product_view(display, number, 0);
    else if (!strcmp(stage, "component_product_draw_oom")) result = test_product_view(display, number, 3);
    else if (!strcmp(stage, "component_product_input")) result = test_product_view(display, number, 4);
    else if (!strcmp(stage, "component_product_line_0")) result = test_product_view(display, number, 5);
    else if (!strcmp(stage, "component_product_line_1")) result = test_product_view(display, number, 6);
    else if (!strcmp(stage, "component_product_line_2")) result = test_product_view(display, number, 7);
    else if (!strcmp(stage, "component_product_boundaries")) result = test_product_view(display, number, 8);
    else if (!strcmp(stage, "component_product")) result = test_product_view(display, number, 1);
    else if (!strcmp(stage, "component_product_render")) result = test_product_view(display, number, 2);
    else if (!strcmp(stage, "component_d11_render")) result = test_product_view(display, number, 9);
    else if (!strcmp(stage, "component_sdk_nav")) result = test_sdk_navigation();
    else if (!strcmp(stage, "component_router")) result = test_router(display, number, false);
    else if (!strcmp(stage, "component_router_oom")) result = test_router(display, number, true);
    else if (!strcmp(stage, "component_navigation")) result = test_component_navigation(number);
    else if (!strcmp(stage, "component_touch")) result = test_touch_input(number);
    else if (!strcmp(stage, "component_gallery")) result = test_component_gallery(number);
    else if (!strcmp(stage, "component_gallery_render")) result = test_component_gallery_render(display, (unsigned)number);
    else if (!strcmp(stage, "component_fill")) result = fill_oom_case(false, number);
    else if (!strcmp(stage, "component_fill_busy")) result = fill_oom_case(true, number);
    else if (!strcmp(stage, "component_render")) result = render_case(display, (unsigned)number);
    else if (!strcmp(stage, "components")) result = lifecycle(display, number);
    else {
        bool draw = !strncmp(stage, "component_draw_", 15);
        unsigned kind = (unsigned)(stage[strlen(stage) - 1] - '0');
        assert(kind < IW_COMPONENT_KIND_COUNT);
        result = failure_case(display, (iw_component_kind_t)kind, draw, number);
    }
    lv_display_delete(display);
    return result;
}
