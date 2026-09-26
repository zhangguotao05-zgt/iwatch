/* 正式 View + 有状态硬件替身；不表示真实 EZIP/EPIC/PSRAM 可见性通过。 */
#include "app_mem.h"
#include "rtthread.h"
#include "iw_product_view.h"
#include "iw_v00_paint_cache.h"
#include "iw_gui_owner.h"
#include "iw_font_port.h"
#include "../../iwatch/src/resource/images/iw_v00_resource_catalog.h"
#include "test_v00_assets.h"
#include "test_v00_fonts.h"
#include "src/draw/lv_image_decoder_private.h"
#include "src/draw/lv_draw_buf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void test_font_owner(bool owner, bool idle);
extern void test_font_arm_failure(size_t index);
extern size_t test_font_allocation_sequence(void);
extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
extern void test_component_capture(lv_display_t *, lv_obj_t *, unsigned);

static union { uint64_t align; uint8_t bytes[2100000]; } psram;
static union { uint64_t align; uint8_t bytes[102904]; } fallback;
struct rt_memheap app_image_psram_memheap = {psram.bytes, sizeof(psram.bytes)};
typedef struct { size_t offset, bytes; bool used, flushed; } block_t;
static block_t blocks[16];
static size_t live_bytes, peak_bytes, alloc_calls, fail_at, flushes;
static bool use_fallback, fallback_live;

void *app_cache_alloc(size_t bytes, unsigned heap)
{
    assert(heap == IMAGE_CACHE_PSRAM && iw_font_port_is_owner());
    ++alloc_calls;
    if (fail_at && alloc_calls == fail_at) return NULL;
    if (use_fallback) {
        assert(bytes <= sizeof(fallback.bytes) && !fallback_live);
        fallback_live = true;
        return fallback.bytes;
    }
    size_t offset = 0;
    for (;;) {
        bool collision = false;
        for (unsigned i = 0; i < 16; ++i) {
            if (blocks[i].used && offset < blocks[i].offset + blocks[i].bytes &&
                offset + bytes > blocks[i].offset) {
                offset = (blocks[i].offset + blocks[i].bytes + 7u) & ~(size_t)7u;
                collision = true;
                break;
            }
        }
        if (!collision) break;
    }
    if (offset > sizeof(psram.bytes) || bytes > sizeof(psram.bytes) - offset) return NULL;
    for (unsigned i = 0; i < 16; ++i) if (!blocks[i].used) {
        blocks[i] = (block_t){offset, bytes, true, false};
        live_bytes += bytes;
        if (peak_bytes < live_bytes) peak_bytes = live_bytes;
        return psram.bytes + offset;
    }
    return NULL;
}

void app_cache_free(void *pixels)
{
    assert(iw_font_port_is_owner() && iw_font_port_render_idle());
    if (pixels == fallback.bytes) { assert(fallback_live); fallback_live = false; return; }
    for (unsigned i = 0; i < 16; ++i) if (blocks[i].used && pixels == psram.bytes + blocks[i].offset) {
        memset(pixels, 0xdd, blocks[i].bytes);
        live_bytes -= blocks[i].bytes;
        blocks[i].used = false;
        return;
    }
    assert(!"Double free or foreign pointer");
}

void app_mem_flush_cache(void *pixels, uint32_t bytes)
{
    assert(iw_font_port_is_owner());
    for (unsigned i = 0; i < 16; ++i) if (blocks[i].used && pixels == psram.bytes + blocks[i].offset) {
        assert(bytes == blocks[i].bytes);
        blocks[i].flushed = true;
        ++flushes;
        return;
    }
    assert(!"Flush outside live PSRAM mock allocation");
}

static int asset_index(const void *src)
{
    for (unsigned i = 0; i < 37; ++i) if (src == iw_v00_resource_catalog_image(i)) return (int)i;
    return -1;
}
static lv_draw_buf_t decoded[37];
static unsigned decoder_open_count, decoder_close_count, decoder_live;
static lv_result_t mock_info(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc, lv_image_header_t *header)
{
    (void)decoder;
    int i = asset_index(dsc->src);
    if (i < 0) return LV_RESULT_INVALID;
    assert(iw_v00_resource_catalog_validate((size_t)i, false) == IW_V00_RESOURCE_OK);
    *header = test_v00_assets_images()[i]->header;
    return LV_RESULT_OK;
}
static lv_result_t mock_open(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc)
{
    (void)decoder;
    int i = asset_index(dsc->src);
    if (i < 0) return LV_RESULT_INVALID;
    dsc->decoded = &decoded[i];
    dsc->header = decoded[i].header;
    ++decoder_open_count;
    ++decoder_live;
    return LV_RESULT_OK;
}
static void mock_close(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc)
{
    (void)decoder;
    assert(asset_index(dsc->src) >= 0 && decoder_live);
    --decoder_live;
    ++decoder_close_count;
}

static iw_product_model_t model(void)
{
    iw_product_model_t m = {
        .clock = {.utc_ms = INT64_C(1789610970000), .valid = true, .revision = 3,
                  .offset_minutes = 480, .source = IW_TIME_SOURCE_RTC},
        .message = IW_TEXT_COUNT, .display_available = true, .time_available = true,
        .lock_available = true, .back = true,
        .face_session = {.schema = IW_FACE_SCHEMA, .active_face_id = IW_FACE_MODULAR_LOCAL,
            .revision = 1, .color = IW_FACE_COLOR_BLUE, .center = IW_FACE_CENTER_NONE,
            .left = IW_FACE_LEFT_SETTINGS, .right = IW_FACE_RIGHT_ABOUT},
        .hardware = "SF32LB58 A128", .firmware = "HOST TARGET BRANCH TEST", .toolchain = "MSVC ASan"};
    m.brightness = (iw_brightness_snapshot_t){.desired = 80, .applied = 80,
        .flags = IW_BRIGHTNESS_FLAG_DESIRED_VALID | IW_BRIGHTNESS_FLAG_APPLIED_VALID};
    m.face_draft = (iw_face_draft_t){.value = m.face_session, .expected_revision = 1, .valid = true};
    assert(iw_time_draft_begin(&m.draft, &m.clock));
    return m;
}

static void recover(void)
{
    test_font_arm_failure(0);
    test_font_owner(true, true);
    if (iw_gui_fault_pending()) { (void)iw_gui_fault_process(); iw_gui_fault_dismiss(); }
    assert(iw_font_collect());
    if (iw_font_fault_pending()) assert(iw_font_ack_fault());
}

static void cache_cases(void)
{
    uint8_t refs[16] = {0};
    for (unsigned i = 0; i < 16; ++i) for (unsigned key = 1; key < IW_V00_PAINT_COUNT; ++key)
        assert(iw_v00_paint_retain(&refs[i], key));
    assert(live_bytes == 423340 && alloc_calls == 6 && flushes == 6);
    /* 最坏共享上界：全部六材质键与 Control 背景同时持有，不按页面数复制。 */
    iw_product_view_t control_view = {0};
    iw_product_model_t control_model = model();
    assert(iw_product_view_create(&control_view, lv_screen_active(), IW_PAGE_CONTROL_CENTER,
                                  &control_model, NULL, NULL, NULL));
    assert(live_bytes == 774340 && alloc_calls == 7 && flushes == 7);
    assert(iw_product_view_destroy(&control_view) && live_bytes == 423340);
    printf("paint keys=6 shared_holders=16 all_bytes=423340 control_gray_bytes=51604 combined_peak=774340 image_descriptor=%zu spec=%zu view=%zu\n",
           sizeof(lv_image_dsc_t), sizeof(iw_v00_paint_spec_t), sizeof(iw_product_view_t));
    for (unsigned key = 1; key < IW_V00_PAINT_COUNT; ++key) {
        const lv_image_dsc_t *a = iw_v00_paint_image(refs[0], key);
        assert(a && a == iw_v00_paint_image(refs[15], key));
        char name[32];
        snprintf(name, sizeof(name), "paint-%u.rgb565a8", key);
        FILE *file = NULL;
        assert(fopen_s(&file, name, "wb") == 0 && file);
        assert(fwrite(a->data, 1, a->data_size, file) == a->data_size);
        assert(fclose(file) == 0);
        uint8_t sentinels[8] = {0x55};
        assert(!iw_v00_paint_pixels(key, sentinels, sizeof(sentinels)) && sentinels[0] == 0x55);
    }
    test_font_owner(true, false);
    assert(!iw_v00_paint_release(&refs[0]) && refs[0] == IW_V00_PAINT_MASK && live_bytes == 423340);
    test_font_owner(false, true);
    assert(!iw_v00_paint_release(&refs[0]) && !iw_v00_paint_retain(&refs[0], 1));
    test_font_owner(true, true);
    for (unsigned i = 0; i < 16; ++i) assert(iw_v00_paint_release(&refs[i]));
    assert(live_bytes == 0 && iw_v00_paint_release(&refs[0]));
    for (unsigned key = 1; key < IW_V00_PAINT_COUNT; ++key) {
        use_fallback = true;
        assert(!iw_v00_paint_retain(&refs[0], key) && !refs[0] && !fallback_live);
        use_fallback = false;
        size_t before = alloc_calls;
        fail_at = alloc_calls + 1;
        assert(!iw_v00_paint_retain(&refs[0], key) && !refs[0]);
        assert(alloc_calls == before + 1);
        printf("paint_oom key=%u reached=1 refs=0 live=0 fallback_rejected=1\n", key);
        fail_at = 0;
        assert(iw_v00_paint_retain(&refs[0], key) && iw_v00_paint_release(&refs[0]));
    }
    assert(!iw_v00_paint_retain(NULL, 1) && !iw_v00_paint_retain(&refs[0], IW_V00_PAINT_COUNT));
    refs[0] = 128;
    assert(!iw_v00_paint_retain(&refs[0], 1) && !iw_v00_paint_release(&refs[0]));
}

int test_target_view(const void *data, size_t size, const char *stage, size_t number)
{
    assert(iw_font_init(data, (uint32_t)size));
    assert(test_v00_fonts_load() && test_v00_assets_load());
    for (unsigned i = 0; i < 37; ++i) {
        if (strcmp(stage, "target_resource_reject"))
            assert(iw_v00_resource_catalog_validate(i, false) == IW_V00_RESOURCE_OK);
        const lv_image_dsc_t *raw = test_v00_assets_images()[i];
        assert(lv_draw_buf_init(&decoded[i], raw->header.w, raw->header.h,
            LV_COLOR_FORMAT_RGB565A8, raw->header.stride, (void *)raw->data, raw->data_size) == LV_RESULT_OK);
    }
    lv_image_decoder_t *decoder = lv_image_decoder_create();
    assert(decoder);
    lv_image_decoder_set_info_cb(decoder, mock_info);
    lv_image_decoder_set_open_cb(decoder, mock_open);
    lv_image_decoder_set_close_cb(decoder, mock_close);
    lv_display_t *display = lv_display_create(390, 450);
    assert(display);
    lv_obj_t *warm = lv_obj_create(lv_screen_active());
    lv_obj_delete(warm);
    iw_product_model_t m = model();
    if (!strcmp(stage, "target_resource_reject")) {
        const uint16_t pages[] = {IW_PAGE_TIMER_LIST, IW_PAGE_DISPLAY, IW_PAGE_LAUNCHER_GRID,
            IW_PAGE_FACE, IW_PAGE_ALARM_EDIT, IW_PAGE_CONTROL_CENTER};
        unsigned invalid = 0;
        for (unsigned i = 0; i < 37; ++i)
            invalid += iw_v00_resource_catalog_validate(i, false) != IW_V00_RESOURCE_OK;
        assert(invalid == 1);
        for (unsigned i = 0; i < 6; ++i) {
            iw_product_view_t view = {0};
            assert(!iw_product_view_create(&view, lv_screen_active(), pages[i], &m, NULL, NULL, NULL));
            assert(!view.frame.object && !view.surface && !view.v00_paint_refs && !live_bytes && !alloc_calls);
            assert(iw_product_view_destroy(&view));
        }
        /* 无效目录必须在数字根切换前拒绝，不能留下不可绘制的模块 scene。 */
        iw_product_view_t root = {0};
        m.face_session.active_face_id = IW_FACE_DIGITAL;
        assert(iw_product_view_create(&root, lv_screen_active(), IW_PAGE_FACE, &m, NULL, NULL, NULL));
        assert(!root.scene.v00_style && !root.v00_images);
        m.face_session.active_face_id = IW_FACE_MODULAR_LOCAL;
        assert(!iw_product_view_update(&root, &m));
        assert(!root.scene.v00_style && !root.v00_images && !root.v00_image_count);
        assert(iw_product_view_destroy(&root));
        recover();
    }
    else if (!strcmp(stage, "target_face_switch")) {
        iw_product_view_t view = {0};
        m.face_session.active_face_id = IW_FACE_DIGITAL;
        assert(iw_product_view_create(&view, lv_screen_active(), IW_PAGE_FACE, &m, NULL, NULL, NULL));
        assert(!view.v00_images && !view.v00_image_count);
        lv_obj_t *surface = view.surface;
        for (unsigned i = 0; i < 4; ++i) {
            m.face_session.active_face_id = IW_FACE_MODULAR_LOCAL;
            assert(iw_product_view_update(&view, &m));
            assert(view.surface == surface && view.v00_images && view.v00_image_count == 37);
            test_component_capture(display, iw_screen_frame_content(&view.frame), 90);
            m.face_session.active_face_id = IW_FACE_DIGITAL;
            assert(iw_product_view_update(&view, &m));
            assert(view.surface == surface && !view.v00_images && !view.v00_image_count);
            test_component_capture(display, iw_screen_frame_content(&view.frame), 91);
        }
        assert(iw_product_view_destroy(&view));
        printf("face_switch digital_modular_roundtrips=4 same_surface=1 result=ok\n");
    }
    else if (!strcmp(stage, "target_cache")) cache_cases();
    else if (!strcmp(stage, "target_control_shared")) {
        iw_product_view_t first = {0}, second = {0};
        assert(iw_product_view_create(&first, lv_screen_active(), IW_PAGE_CONTROL_CENTER, &m, NULL, NULL, NULL));
        assert(iw_product_view_create(&second, lv_screen_active(), IW_PAGE_CONTROL_CENTER, &m, NULL, NULL, NULL));
        assert(second.v00_images[37]->data && live_bytes == 402604);
        assert(iw_product_view_destroy(&first));
        printf("control_second_live=%d background_present=%d psram_live=%zu\n",
            second.surface != NULL, second.v00_images[37]->data != NULL, live_bytes);
        fflush(stdout);
        assert(second.v00_images[37]->data != NULL);
        assert(iw_product_view_destroy(&second));
    } else if (!strcmp(stage, "target_control_edges") || !strcmp(stage, "target_control_oom")) {
        iw_product_view_t first = {0}, second = {0};
        lv_obj_t *parent = lv_obj_create(lv_screen_active());
        assert(parent);
        assert(iw_product_view_create(&first, parent, IW_PAGE_CONTROL_CENTER, &m, NULL, NULL, NULL));
        const uint8_t *pixels = first.v00_images[37]->data;
        size_t allocated = alloc_calls;
        if (!strcmp(stage, "target_control_oom")) {
            size_t before = test_font_allocation_sequence();
            test_font_arm_failure(number);
            bool created = iw_product_view_create(&second, lv_screen_active(), IW_PAGE_CONTROL_CENTER, &m, NULL, NULL, NULL);
            size_t reached = test_font_allocation_sequence() - before;
            test_font_arm_failure(0);
            assert(first.surface && first.v00_images[37]->data == pixels && live_bytes == 402604);
            assert(iw_product_view_destroy(&second) && iw_product_view_destroy(&second));
            assert(first.v00_images[37]->data == pixels && live_bytes == 402604 && alloc_calls == allocated);
            printf("control_shared_oom point=%zu allocations=%zu created=%u reached=%u\n",
                number, reached, (unsigned)created, (unsigned)(reached >= number));
        } else {
            test_font_owner(true, false);
            assert(!iw_product_view_create(&second, lv_screen_active(), IW_PAGE_CONTROL_CENTER, &m, NULL, NULL, NULL));
            assert(!second.control_cache_owned && alloc_calls == allocated);
            test_font_owner(true, true);
            assert(iw_product_view_create(&second, lv_screen_active(), IW_PAGE_CONTROL_CENTER, &m, NULL, NULL, NULL));
            assert(alloc_calls == allocated && first.control_cache_owned && second.control_cache_owned);
            /* 父删忙态不得丢失持有状态；句柄不能提前重新用于另一个页面。 */
            test_font_owner(true, false);
            lv_obj_delete(parent);
            parent = NULL;
            assert(!first.surface && first.control_cache_owned && !iw_product_view_destroy(&first));
            assert(!iw_product_view_create(&first, lv_screen_active(), IW_PAGE_DISPLAY, &m, NULL, NULL, NULL));
            assert(second.v00_images[37]->data == pixels && live_bytes == 402604);
            test_font_owner(true, true);
            /* 相反销毁顺序仍保留忙态父删留下的最后持有者。 */
            assert(iw_product_view_destroy(&second) && iw_product_view_destroy(&second));
            assert(first.v00_images[37]->data == pixels && first.control_cache_owned && live_bytes == 402604);
        }
        assert(iw_product_view_destroy(&first) && !first.control_cache_owned && !live_bytes);
        if (parent) lv_obj_delete(parent);
        recover();
        assert(iw_product_view_create(&first, lv_screen_active(), IW_PAGE_CONTROL_CENTER, &m, NULL, NULL, NULL));
        assert(iw_product_view_destroy(&second));
        assert(first.v00_images[37]->data && live_bytes == 402604);
        assert(iw_product_view_destroy(&first) && !live_bytes);
    } else {
        const uint16_t pages[] = {IW_PAGE_TIMER_LIST, IW_PAGE_DISPLAY, IW_PAGE_LAUNCHER_GRID,
            IW_PAGE_FACE, IW_PAGE_ALARM_EDIT, IW_PAGE_CONTROL_CENTER};
        bool oom = !strcmp(stage, "target_oom"), cache_oom = !strcmp(stage, "target_cache_oom");
        bool parent_busy = !strcmp(stage, "target_parent_busy");
        unsigned loops = (oom || cache_oom) ? 1u : (unsigned)(number ? number : 1);
        for (unsigned loop = 0; loop < loops; ++loop) for (unsigned p = 0; p < (parent_busy ? 2u : 6u); ++p) {
            iw_product_view_t view = {0};
            size_t bytes_before = test_font_live_bytes(), blocks_before = test_font_live_blocks();
            lv_obj_t *parent = lv_obj_create(lv_screen_active());
            assert(parent);
            /* 创建/销毁使用独立父屏，绘制时必须与正式 390×450 视口一致。 */
            lv_obj_remove_style_all(parent);
            lv_obj_set_size(parent, 390, 450);
            size_t before_sequence = test_font_allocation_sequence();
            test_font_arm_failure(oom ? number : 0);
            fail_at = cache_oom ? alloc_calls + number : 0;
            bool created = iw_product_view_create(&view, parent, pages[p], &m, NULL, NULL, NULL);
            size_t sequence = test_font_allocation_sequence() - before_sequence;
            test_font_arm_failure(0);
            fail_at = 0;
            if (!oom && !cache_oom) assert(created);
            if (created) {
                assert(view.v00_image_count == (p == 5 ? 38 : 37));
                assert(view.v00_images[0] == iw_v00_resource_catalog_image(0));
                assert(iw_product_view_update(&view, &m));
                if (p == 0) assert(view.v00_paint_refs == 1);
                if (p == 1) assert(view.v00_paint_refs == 6);
                if (!oom && !cache_oom && !strcmp(stage, "target_draw"))
                    test_component_capture(display, iw_screen_frame_content(&view.frame), 80 + p);
                test_font_owner(true, false);
                assert(!iw_product_view_destroy(&view) && view.surface);
                if (parent_busy) {
                    uint8_t retained = view.v00_paint_refs;
                    lv_obj_delete(parent);
                    assert(!view.surface && view.v00_paint_refs == retained && live_bytes);
                    assert(!iw_product_view_destroy(&view));
                }
                test_font_owner(true, true);
                if (!parent_busy && (loop & 1u)) lv_obj_delete(parent);
                assert(iw_product_view_destroy(&view));
                if (!parent_busy && !(loop & 1u)) lv_obj_delete(parent);
            } else {
                assert(iw_product_view_destroy(&view));
                lv_obj_delete(parent);
            }
            recover();
            assert(!view.surface && !view.frame.object && !view.v00_paint_refs && !live_bytes && !fallback_live);
            assert(test_font_live_bytes() == bytes_before && test_font_live_blocks() == blocks_before);
            printf("page=%u created=%d create_allocations=%zu loop=%u\n", pages[p], created, sequence, loop);
            if (oom || cache_oom) {
                assert(iw_product_view_create(&view, lv_screen_active(), pages[p], &m, NULL, NULL, NULL));
                assert(iw_product_view_destroy(&view));
                recover();
                assert(!live_bytes);
            }
        }
    }
    assert(!decoder_live && decoder_open_count == decoder_close_count && !live_bytes);
    lv_display_delete(display);
    lv_image_decoder_delete(decoder);
    recover();
    test_v00_fonts_release();
    test_v00_assets_release();
    printf("stage=%s number=%zu psram_peak=%zu flushes=%zu decoder_open=%u decoder_close=%u result=ok\n",
        stage, number, peak_bytes, flushes, decoder_open_count, decoder_close_count);
    return 0;
}
