#ifndef IW_FONT_H
#define IW_FONT_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IW_FONT_16, IW_FONT_18, IW_FONT_20, IW_FONT_22, IW_FONT_24, IW_FONT_26,
    IW_FONT_28, IW_FONT_30, IW_FONT_32, IW_FONT_48, IW_FONT_64, IW_FONT_80, IW_FONT_96,
    IW_FONT_COUNT
} iw_font_id_t;

typedef enum {
    IW_FONT_OK, IW_FONT_INVALID, IW_FONT_NOT_READY, IW_FONT_WRONG_OWNER, IW_FONT_CREATE_FAILED,
    IW_FONT_FAULTED, IW_FONT_LIMIT, IW_FONT_STALE
} iw_font_result_t;

typedef struct {
    uint8_t size_px;
    uint8_t glyph_entries;
    bool legacy;
} iw_font_spec_t;

/* 调用方零初始化，禁止复制、修改或共享可变句柄；每次 acquire 对应一次 release。 */
typedef struct {
    const lv_font_t *font;
    uint32_t generation;
    iw_font_id_t id;
} iw_font_ref_t;

typedef struct {
    const void *data;
    uint32_t bytes;
} iw_font_blob_t;

typedef struct {
    uint32_t main_used, main_peak, main_total;
    uint32_t ttf_used, ttf_peak, ttf_total;
} iw_font_memory_t;

typedef struct {
    uint32_t create_oom, metadata_oom, bitmap_oom, fallback_shown;
    uint32_t sessions, references, live, peak_live;
    uint32_t main_baseline, ttf_baseline, ttf_idle_delta;
    uint32_t main_peak_delta, ttf_peak_delta;
    bool pending;
    iw_font_memory_t memory;
} iw_font_stats_t;

/* 只读注册表不依赖初始化；IW_FONT_COUNT 是无效字号哨兵。 */
const iw_font_spec_t *iw_font_spec(iw_font_id_t id);
iw_font_id_t iw_font_find(uint16_t size_px);
/* 仅在 GUI 线程、lv_init 之后调用；源数据必须保持有效直到服务结束。 */
bool iw_font_init(const void *data, uint32_t bytes);
/* V00 四份静态字重均已完整校验后一次注册；只在 GUI owner 调用。 */
bool iw_font_v00_init(const iw_font_blob_t fonts[4]);
/* 所有 V00 引用和字体对象均回收后解除注册；源缓冲此后可由调用方释放。 */
bool iw_font_v00_deinit(void);
iw_font_result_t iw_font_acquire(iw_font_id_t id, iw_font_ref_t *ref);
/* 文件身份、字重和字号组成独立缓存键；不会静默回退到旧字库。 */
iw_font_result_t iw_font_acquire_v00(uint16_t weight, uint16_t size_px, iw_font_ref_t *ref);
/* 先删除引用此字体的控件，再 release；空句柄重复释放成功。 */
iw_font_result_t iw_font_release(iw_font_ref_t *ref);
/* 仅 GUI 线程在 LVGL 回调之外调用；未排空时延后零引用字体的销毁。 */
bool iw_font_collect(void);
void iw_font_sample(void);
bool iw_font_fault_pending(void);
/* 故障 owner 先释放全部使用方；仍有存活字体时不允许清除故障。 */
bool iw_font_ack_fault(void);
void iw_font_note_fallback(void);
/* 读取最近发布的一致快照，允许诊断线程调用，不访问 LVGL。 */
void iw_font_get_stats(iw_font_stats_t *stats);

#endif
