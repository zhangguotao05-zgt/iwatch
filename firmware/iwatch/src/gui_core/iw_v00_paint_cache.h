#ifndef IW_V00_PAINT_CACHE_H
#define IW_V00_PAINT_CACHE_H
#include "lvgl.h"
#include "iw_v00_paint.h"

/* 仅 GUI owner 调用；位集由 View 持有，忙态释放失败时必须保留并重试。 */
bool iw_v00_paint_retain(uint8_t *references, unsigned id);
bool iw_v00_paint_release(uint8_t *references);
const lv_image_dsc_t *iw_v00_paint_image(uint8_t references, unsigned id);
#endif
