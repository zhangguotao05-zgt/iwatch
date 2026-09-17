#include "iw_font.h"
#include "iw_font_port.h"
#include "iw_theme.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 直接编译生产文件，白盒仅用于不可遍历的计数饱和边界；不改固件测试开关。 */
#include "iw_font.c"

extern void test_font_arm_failure(size_t index);
extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
extern size_t test_font_allocation_sequence(void);
extern unsigned test_font_assert_count(void);
extern int test_epic_glyph(lv_font_glyph_dsc_t *glyph);
extern unsigned test_epic_submissions(void);

static bool test_owner = true, test_render_idle = true;
void test_font_owner(bool owner, bool idle) { test_owner = owner; test_render_idle = idle; }
static iw_font_stats_t snapshot;
static uint32_t main_peak;

bool iw_font_port_bind_owner(void) { return test_owner; }
bool iw_font_port_is_owner(void) { return test_owner; }
bool iw_font_port_render_idle(void) { return test_render_idle; }
void iw_font_port_memory(iw_font_memory_t *memory)
{
    uint32_t used = (uint32_t)test_font_live_bytes();
    if (used > main_peak) main_peak = used;
    /* 主机使用同一分配器；PSRAM 独立数值用于检查统计通道，实物数据另测。 */
    *memory = (iw_font_memory_t){used,main_peak,2u*1024u*1024u,72,96,160000};
}
void iw_font_port_publish(const iw_font_stats_t *value) { snapshot = *value; }
void iw_font_port_read(iw_font_stats_t *value) { *value = snapshot; }

static void theme_contract(void)
{
    const iw_theme_t *value = iw_theme_get();
    assert(value == iw_theme_get() && value->width == 390 && value->height == 450);
    assert(value->width - 2u*value->inset_x == 354 && value->hit_size >= value->icon_size);
    assert(value->button_height >= 2u*value->button_radius);
    assert(!iw_theme_effects((iw_theme_quality_t)-1,false));
    assert(!iw_theme_effects(IW_THEME_QUALITY_COUNT,false));
    assert(iw_theme_font_px((iw_theme_font_role_t)-1) == 0 && iw_theme_font_px(IW_THEME_FONT_COUNT) == 0);
    for (unsigned i = 0; i < IW_THEME_FONT_COUNT; ++i) {
        iw_font_id_t id = iw_font_find(iw_theme_font_px((iw_theme_font_role_t)i));
        assert(id != IW_FONT_COUNT && !iw_font_spec(id)->legacy);
    }
    for (unsigned i = 0; i < IW_THEME_QUALITY_COUNT; ++i) {
        const iw_theme_effects_t *normal = iw_theme_effects((iw_theme_quality_t)i,false);
        const iw_theme_effects_t *reduced = iw_theme_effects((iw_theme_quality_t)i,true);
        assert(normal->press_ms >= 60 && normal->press_ms <= 90);
        assert(normal->pressed_scale_permille >= 960 && normal->pressed_scale_permille <= 980);
        assert(reduced->press_ms == 0 && reduced->navigation_ms == 0 && reduced->app_transition_ms == 0);
        assert(reduced->sheet_ms == 0 && reduced->snap_ms == 0 && reduced->value_ms == 0 && reduced->toggle_ms == 0);
        assert(reduced->pressed_scale_permille == 1000 && normal->surface_opacity == reduced->surface_opacity);
    }
    assert(iw_theme_effects(IW_THEME_Q0,false)->surface_opacity == 255);
    assert(!iw_theme_effects(IW_THEME_Q0,false)->highlight_gradient);
    assert(iw_theme_effects(IW_THEME_Q1,false)->highlight_gradient);
}

static int registry_oom(size_t point, bool epic)
{
    size_t initial_blocks = test_font_live_blocks(), initial_bytes = test_font_live_bytes();
    iw_font_ref_t ref = {0}, other = {0};
    lv_font_glyph_dsc_t glyph = {0};
    if (epic) {
        assert(iw_font_acquire(IW_FONT_96,&ref) == IW_FONT_OK);
        assert(lv_font_get_glyph_dsc(ref.font,&glyph,0x601d,0));
    }
    unsigned submitted = test_epic_submissions();
    size_t begin = test_font_allocation_sequence();
    test_font_arm_failure(point);
    iw_font_result_t result = epic ? IW_FONT_OK : iw_font_acquire(IW_FONT_96,&ref);
    if (epic) assert(test_epic_glyph(&glyph) == 0);
    test_font_arm_failure(0);
    size_t allocations = test_font_allocation_sequence() - begin;
    if (point) {
        assert(iw_font_fault_pending());
        assert(iw_font_acquire(IW_FONT_20,&other) == IW_FONT_FAULTED && !other.font);
        if (epic) {
            assert(stats.bitmap_oom == 1 && test_epic_submissions() == submitted);
            assert(test_epic_glyph(&glyph) == 0 && stats.bitmap_oom == 1);
            assert(!iw_font_ack_fault());
        }
        else assert(result == IW_FONT_CREATE_FAILED && !ref.font && stats.create_oom == 1);
    }
    else assert(result == IW_FONT_OK && !iw_font_fault_pending());
    assert(iw_font_release(&ref) == IW_FONT_OK);
    if (epic) {
        test_render_idle = false;
        assert(!iw_font_collect() && stats.live == 1 && !iw_font_ack_fault());
    }
    test_render_idle = true;
    assert(iw_font_collect() && iw_font_ack_fault());
    assert(iw_font_acquire(IW_FONT_96,&ref) == IW_FONT_OK);
    assert(iw_font_release(&ref) == IW_FONT_OK && iw_font_collect());
    assert(test_font_live_bytes() == initial_bytes && test_font_live_blocks() == initial_blocks);
    assert(test_font_assert_count() == 0);
    printf("stage=%s allocations=%zu oom=%u asserts=0 result=ok\n", epic ? "registry_epic" : "registry_oom",
           allocations, stats.create_oom + stats.bitmap_oom);
    return 0;
}

static int registry_lifecycle(size_t loops)
{
    size_t initial_blocks = test_font_live_blocks(), initial_bytes = test_font_live_bytes();
    theme_contract();
    assert(!iw_font_spec((iw_font_id_t)-1) && !iw_font_spec(IW_FONT_COUNT));
    assert(iw_font_find(17) == IW_FONT_COUNT);
    for (size_t loop = 0; loop < loops; ++loop) {
        iw_font_ref_t first[IW_FONT_COUNT] = {0}, second[IW_FONT_COUNT] = {0};
        for (unsigned i = 0; i < IW_FONT_COUNT; ++i) {
            iw_font_id_t id = (iw_font_id_t)i;
            const iw_font_spec_t *spec = iw_font_spec(id);
            assert(iw_font_find(spec->size_px) == id);
            assert(spec->glyph_entries == (spec->size_px >= 64 ? 4 : 16));
            assert(iw_font_acquire(id,&first[i]) == IW_FONT_OK);
            size_t allocations = test_font_allocation_sequence();
            assert(iw_font_acquire(id,&second[i]) == IW_FONT_OK);
            assert(test_font_allocation_sequence() == allocations && first[i].font == second[i].font);
            if (!loop) {
                for (uint32_t ch = '0'; ch <= '9'; ++ch) {
                    lv_font_glyph_dsc_t glyph = {0};
                    assert(lv_font_get_glyph_dsc(first[i].font,&glyph,ch,0));
                    assert(test_epic_glyph(&glyph) == 0);
                }
            }
        }
        assert(stats.live == IW_FONT_COUNT && stats.references == 2u*IW_FONT_COUNT);
        test_owner = false;
        assert(iw_font_release(&first[0]) == IW_FONT_WRONG_OWNER && !iw_font_collect());
        iw_font_ref_t wrong_owner = {0};
        assert(iw_font_acquire(IW_FONT_20,&wrong_owner) == IW_FONT_WRONG_OWNER && !wrong_owner.font);
        test_owner = true;
        iw_font_ref_t stale = first[0];
        for (unsigned i = 0; i < IW_FONT_COUNT; ++i) assert(iw_font_release(&first[i]) == IW_FONT_OK);
        assert(iw_font_collect() && stats.live == IW_FONT_COUNT);
        for (unsigned i = 0; i < IW_FONT_COUNT; ++i) {
            assert(iw_font_release(&second[i]) == IW_FONT_OK);
            assert(iw_font_release(&second[i]) == IW_FONT_OK);
        }
        test_render_idle = false;
        assert(!iw_font_collect() && stats.live == IW_FONT_COUNT && stats.references == 0);
        test_render_idle = true;
        assert(iw_font_collect() && stats.live == 0);
        assert(iw_font_acquire(IW_FONT_16,&first[0]) == IW_FONT_OK);
        assert(iw_font_release(&stale) == IW_FONT_STALE && stats.references == 1);
        assert(iw_font_release(&first[0]) == IW_FONT_OK && iw_font_collect());
        assert(test_font_live_bytes() == initial_bytes && test_font_live_blocks() == initial_blocks);
    }
    iw_font_ref_t ref = {0}, other = {0};
    uint32_t saved_generation = slots[IW_FONT_20].generation;
    slots[IW_FONT_20].generation = UINT32_MAX;
    assert(iw_font_acquire(IW_FONT_20,&ref) == IW_FONT_LIMIT && !ref.font);
    slots[IW_FONT_20].generation = saved_generation;
    assert(iw_font_acquire(IW_FONT_20,&ref) == IW_FONT_OK);
    slots[IW_FONT_20].references = UINT16_MAX;
    assert(iw_font_acquire(IW_FONT_20,&other) == IW_FONT_LIMIT && !other.font);
    slots[IW_FONT_20].references = 1;
    stats.bitmap_oom = UINT32_MAX;
    lv_tiny_ttf_report_draw_oom(ref.font);
    assert(stats.bitmap_oom == UINT32_MAX && stats.pending);
    assert(iw_font_release(&ref) == IW_FONT_OK && iw_font_ack_fault());
    iw_font_note_fallback();
    iw_font_get_stats(&snapshot);
    assert(snapshot.fallback_shown == 1 && snapshot.references == 0 && snapshot.live == 0);
    assert(test_font_live_bytes() == initial_bytes && test_font_live_blocks() == initial_blocks);
    assert(test_font_assert_count() == 0);
    printf("stage=registry loops=%zu sizes=%u main_used=%zu peak=%u asserts=0 result=ok\n",
           loops,(unsigned)IW_FONT_COUNT,initial_bytes,main_peak);
    return 0;
}

int test_font_service(const void *data, size_t size, const char *stage, size_t number)
{
    assert(size <= UINT32_MAX);
    iw_font_ref_t empty = {0};
    assert(iw_font_acquire(IW_FONT_20,&empty) == IW_FONT_NOT_READY);
    assert(!iw_font_init(NULL,0) && iw_font_init(data,(uint32_t)size));
    assert(iw_font_init(data,(uint32_t)size) && !iw_font_init(data,(uint32_t)size - 1));
    if (strcmp(stage,"registry_oom") == 0) return registry_oom(number,false);
    if (strcmp(stage,"registry_epic") == 0) return registry_oom(number,true);
    if (strcmp(stage,"registry") == 0) return registry_lifecycle(number);
    return 67;
}
