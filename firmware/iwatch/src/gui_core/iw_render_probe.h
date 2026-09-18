#ifndef IW_RENDER_PROBE_H
#define IW_RENDER_PROBE_H
#include <stdbool.h>
#include <stdint.h>

#define IW_RENDER_PROBE_CAPACITY 128u
typedef struct {
    uint32_t begin_ms, submit_ms, idle_ms, first_ms;
    uint16_t page_id;
    bool first, idle_seen;
} iw_render_sample_t;
/* 全部调用限定 GUI 线程。记录 LVGL 绘制批次及空闲观察，不代表面板扫描完成。 */
void iw_render_probe_start(void);
void iw_render_probe_stop(void);
bool iw_render_probe_active(void);
void iw_render_probe_begin(uint32_t now, bool render_idle);
void iw_render_probe_draw(uint16_t page, uint32_t first_ms, bool first);
void iw_render_probe_end(uint32_t now, bool render_idle);
const iw_render_sample_t *iw_render_probe_samples(unsigned *count);
#endif
