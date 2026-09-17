#include "iw_font_port.h"
#include <rtthread.h>
#include <rthw.h>
#include "rtconfig.h"

/* 固定 SDK 的 lv_lcd.c 提供此函数，公共头文件未声明。 */
extern bool lv_refreshing_done(void);
#if defined(TINY_TTF_CACHE_IN_SRAM_STANDALONE) || defined(TINY_TTF_CACHE_IN_PSRAM)
extern struct rt_memheap app_tiny_ttf_memheap;
#endif

static rt_thread_t owner;
static iw_font_stats_t published;

bool iw_font_port_bind_owner(void)
{
    if (rt_interrupt_get_nest() || !rt_thread_self()) return false;
    if (!owner) owner = rt_thread_self();
    return owner == rt_thread_self();
}

bool iw_font_port_is_owner(void)
{
    return owner && !rt_interrupt_get_nest() && owner == rt_thread_self();
}

bool iw_font_port_render_idle(void)
{
    return lv_refreshing_done();
}

void iw_font_port_memory(iw_font_memory_t *memory)
{
    rt_memory_info(&memory->main_total, &memory->main_used, &memory->main_peak);
#if defined(TINY_TTF_CACHE_IN_SRAM_STANDALONE) || defined(TINY_TTF_CACHE_IN_PSRAM)
    memory->ttf_used = app_tiny_ttf_memheap.pool_size - app_tiny_ttf_memheap.available_size;
    memory->ttf_peak = app_tiny_ttf_memheap.max_used_size;
    memory->ttf_total = app_tiny_ttf_memheap.pool_size;
#else
    memory->ttf_used = memory->ttf_peak = memory->ttf_total = 0;
#endif
}

void iw_font_port_publish(const iw_font_stats_t *value)
{
    /* 临界区只复制固定长度快照，不分配、不采样硬件、不调用 LVGL。 */
    rt_base_t level = rt_hw_interrupt_disable();
    published = *value;
    rt_hw_interrupt_enable(level);
}

void iw_font_port_read(iw_font_stats_t *value)
{
    rt_base_t level = rt_hw_interrupt_disable();
    *value = published;
    rt_hw_interrupt_enable(level);
}

static void iw_font_stat(void)
{
    iw_font_stats_t value;
    iw_font_get_stats(&value);
    /* 当前堆值用于桌面稳定样本，其余字段取最近的 GUI owner 快照。 */
    iw_font_port_memory(&value.memory);
    rt_kprintf("font cache glyph=16 kerning=0 enters=%u live=%u pending=%u\n",
               value.sessions, value.live, (unsigned)value.pending);
    rt_kprintf("font oom create=%u metadata=%u bitmap=%u fallback=%u\n",
               value.create_oom, value.metadata_oom, value.bitmap_oom, value.fallback_shown);
    rt_kprintf("font main base=%u used=%u global_max=%u total=%u\n",
               value.main_baseline, value.memory.main_used, value.memory.main_peak, value.memory.main_total);
    rt_kprintf("font ttf base=%u used=%u peak=%u idle_delta=%u\n",
               value.ttf_baseline, value.memory.ttf_used, value.memory.ttf_peak, value.ttf_idle_delta);
    rt_kprintf("font registry sizes=%u refs=%u peak_live=%u large_glyph=4 peak_delta=%u/%u\n",
               (unsigned)IW_FONT_COUNT, value.references, value.peak_live, value.main_peak_delta, value.ttf_peak_delta);
}
MSH_CMD_EXPORT(iw_font_stat, Show font registry and separate memory pools);
