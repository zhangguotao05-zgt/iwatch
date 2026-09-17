#ifndef IW_TOUCH_H
#define IW_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t overflows, discarded, cancellations, recoveries;
    uint8_t queued, high_water;
    bool cancel_pending, wait_release, physical_down;
} iw_touch_stats_t;

/* 全部快照在驱动互斥锁内复制；累计值饱和到 UINT32_MAX，仅重启清零。 */
bool iw_touch_stats_get(iw_touch_stats_t *out);
/* GUI 已重置 LVGL 后调用：丢弃旧样本，直到读到真实释放才允许新手势。 */
void iw_touch_cancel(void);

#endif
