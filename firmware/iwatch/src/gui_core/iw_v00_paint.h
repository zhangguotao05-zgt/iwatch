#ifndef IW_V00_PAINT_H
#define IW_V00_PAINT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 不可变材质键共享像素；位置和页面实例不产生新缓存。 */
enum { IW_V00_PAINT_NONE, IW_V00_PAINT_TIMER, IW_V00_PAINT_BRIGHTNESS,
       IW_V00_PAINT_ROW, IW_V00_PAINT_CONTROL_GRAY, IW_V00_PAINT_CONTROL_BLUE,
       IW_V00_PAINT_CONTROL_PURPLE, IW_V00_PAINT_COUNT };
enum { IW_V00_PAINT_MASK = (1u << (IW_V00_PAINT_COUNT - 1u)) - 1u };
typedef struct {
    uint16_t width, height, radius_half_px, angle_deg;
    uint16_t gradient_width, gradient_height, inset_px;
    uint32_t start_color, end_color;
    uint32_t border_color;
    uint8_t fill_opacity, border_opacity;
} iw_v00_paint_spec_t;

const iw_v00_paint_spec_t *iw_v00_paint_spec(unsigned id);
/* 输出为连续 RGB565 色平面和 A8 平面，调用者拥有完整缓冲；不分配内存。 */
bool iw_v00_paint_pixels(unsigned id, uint8_t *pixels, size_t capacity);
#endif
