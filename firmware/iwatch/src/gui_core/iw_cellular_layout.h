#ifndef IW_CELLULAR_LAYOUT_H
#define IW_CELLULAR_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

#define IW_CELLULAR_ICON_COUNT 17u

typedef struct {
    int16_t x, y;
    uint8_t size;
    uint16_t action;
} iw_cellular_icon_t;

/* 蜂窝布局由锁定版 SiFli SDK 的 cell_transform 计算；静态基准坐标单独保留。 */
bool iw_cellular_layout(int pan_x, int pan_y, int zoom, bool runtime,
                        iw_cellular_icon_t icons[IW_CELLULAR_ICON_COUNT]);

#endif
