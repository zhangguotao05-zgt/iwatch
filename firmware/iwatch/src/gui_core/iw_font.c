#include "iw_font.h"
#include "iw_font_port.h"
#include <string.h>

/* 字距缓存沿用 A1 已验收配置，禁止重新启用存在淘汰缺陷的 SDK 分支。 */
#if LV_TINY_TTF_CACHE_KERNING_CNT != 0 || LV_USE_OS != LV_OS_NONE
    #error "iwatch fonts require zero kerning cache and synchronous LVGL rendering"
#endif

static const iw_font_spec_t specs[IW_FONT_COUNT] = {
    {16,16,true}, {18,16,true}, {20,16,false}, {22,16,false}, {24,16,true},
    {26,16,false}, {28,16,true}, {30,16,false}, {64,4,false}, {80,4,false}, {96,4,false}
};

typedef struct {
    lv_font_t *font;
    uint32_t generation;
    uint16_t references;
} font_slot_t;

static font_slot_t slots[IW_FONT_COUNT];
static const void *font_data;
static uint32_t font_bytes;
static iw_font_stats_t stats;

static void increment(uint32_t *value)
{
    if (*value != UINT32_MAX) ++*value;
}

static uint32_t delta(uint32_t used, uint32_t baseline)
{
    return used > baseline ? used - baseline : 0;
}

static void on_oom(lv_tiny_ttf_oom_reason_t reason, void *context)
{
    (void)context;
    if (reason <= LV_TINY_TTF_OOM_KERNING_CACHE) increment(&stats.create_oom);
    else if (reason == LV_TINY_TTF_OOM_GLYPH_METADATA) increment(&stats.metadata_oom);
    else increment(&stats.bitmap_oom);
    /* SDK 回调只写计数和锁存；不调用 LVGL、不打印、不释放资源。 */
    stats.pending = true;
}

const iw_font_spec_t *iw_font_spec(iw_font_id_t id)
{
    return (unsigned)id < IW_FONT_COUNT ? &specs[id] : NULL;
}

iw_font_id_t iw_font_find(uint16_t size_px)
{
    for (unsigned i = 0; i < IW_FONT_COUNT; ++i)
        if (specs[i].size_px == size_px) return (iw_font_id_t)i;
    return IW_FONT_COUNT;
}

bool iw_font_init(const void *data, uint32_t bytes)
{
    if (!data || !bytes || !iw_font_port_bind_owner()) return false;
    if (font_data) return data == font_data && bytes == font_bytes;
    font_data = data;
    font_bytes = bytes;
    lv_tiny_ttf_set_oom_cb(on_oom, NULL);
    iw_font_sample();
    return true;
}

iw_font_result_t iw_font_acquire(iw_font_id_t id, iw_font_ref_t *ref)
{
    if (!ref || ref->font || !iw_font_spec(id)) return IW_FONT_INVALID;
    if (!font_data) return IW_FONT_NOT_READY;
    if (!iw_font_port_is_owner()) return IW_FONT_WRONG_OWNER;
    if (stats.pending) return IW_FONT_FAULTED;
    font_slot_t *slot = &slots[id];
    if (slot->references == UINT16_MAX || (!slot->font && slot->generation == UINT32_MAX))
        return IW_FONT_LIMIT;
    if (!stats.references) {
        iw_font_port_memory(&stats.memory);
        stats.main_baseline = stats.memory.main_used;
        stats.ttf_baseline = stats.memory.ttf_used;
        increment(&stats.sessions);
    }
    if (!slot->font) {
        slot->font = lv_tiny_ttf_create_data_ex(font_data, font_bytes, specs[id].size_px,
                                               LV_FONT_KERNING_NORMAL, specs[id].glyph_entries);
        if (!slot->font) {
            /* 非 OOM 的无效字体输入也要阻止继续创建，不伪造 OOM 计数。 */
            stats.pending = true;
            iw_font_sample();
            return IW_FONT_CREATE_FAILED;
        }
        ++slot->generation;
        ++stats.live;
        if (stats.live > stats.peak_live) stats.peak_live = stats.live;
    }
    ++slot->references;
    ++stats.references;
    *ref = (iw_font_ref_t){slot->font, slot->generation, id};
    iw_font_sample();
    return IW_FONT_OK;
}

iw_font_result_t iw_font_release(iw_font_ref_t *ref)
{
    if (!ref) return IW_FONT_INVALID;
    if (!iw_font_port_is_owner()) return IW_FONT_WRONG_OWNER;
    if (!ref->font) return IW_FONT_OK;
    if (!iw_font_spec(ref->id)) return IW_FONT_STALE;
    font_slot_t *slot = &slots[ref->id];
    if (!slot->references || slot->font != ref->font || slot->generation != ref->generation)
        return IW_FONT_STALE;
    --slot->references;
    --stats.references;
    memset(ref, 0, sizeof(*ref));
    /* 这里只释放引用；GPU 可能仍持有字形数据，不能立即销毁字体。 */
    iw_font_port_publish(&stats);
    return IW_FONT_OK;
}

bool iw_font_collect(void)
{
    if (!font_data || !iw_font_port_is_owner()) return false;
    bool idle_checked = false;
    for (unsigned i = 0; i < IW_FONT_COUNT; ++i) {
        font_slot_t *slot = &slots[i];
        if (!slot->font || slot->references) continue;
        if (!idle_checked && !iw_font_port_render_idle()) return false;
        idle_checked = true;
        lv_tiny_ttf_destroy(slot->font);
        slot->font = NULL;
        --stats.live;
    }
    if (idle_checked) iw_font_sample();
    return true;
}

void iw_font_sample(void)
{
    if (!font_data || !iw_font_port_is_owner()) return;
    iw_font_port_memory(&stats.memory);
    /* 全局堆高水位给出保守上界；不把文件字节数当作运行内存。 */
    if (stats.sessions) {
        uint32_t main_delta = delta(stats.memory.main_peak, stats.main_baseline);
        uint32_t ttf_delta = delta(stats.memory.ttf_peak, stats.ttf_baseline);
        if (main_delta > stats.main_peak_delta) stats.main_peak_delta = main_delta;
        if (ttf_delta > stats.ttf_peak_delta) stats.ttf_peak_delta = ttf_delta;
        if (!stats.live) stats.ttf_idle_delta = delta(stats.memory.ttf_used, stats.ttf_baseline);
    }
    iw_font_port_publish(&stats);
}

bool iw_font_fault_pending(void)
{
    return iw_font_port_is_owner() && stats.pending;
}

bool iw_font_ack_fault(void)
{
    if (!font_data || !iw_font_port_is_owner() || !iw_font_collect() || stats.live) return false;
    stats.pending = false;
    iw_font_port_publish(&stats);
    return true;
}

void iw_font_note_fallback(void)
{
    if (!iw_font_port_is_owner()) return;
    increment(&stats.fallback_shown);
    iw_font_port_publish(&stats);
}

void iw_font_get_stats(iw_font_stats_t *output)
{
    if (output) iw_font_port_read(output);
}
