#ifndef IW_DRAW_CHECKED_H
#define IW_DRAW_CHECKED_H
#include "lvgl.h"
/* 文本按任务复制，失败只锁存故障，回收由 GUI 安全点完成。 */
bool iw_draw_fill_checked(lv_layer_t *layer, const lv_area_t *area, uint32_t color,
    int32_t radius, uint8_t opacity, bool gradient, uint32_t end_color);
bool iw_draw_text_checked(lv_layer_t *layer, const lv_area_t *area, const char *text,
    const lv_font_t *font, uint32_t color, lv_text_align_t align, uint8_t opacity);
bool iw_draw_text_checked_spaced(lv_layer_t *layer, const lv_area_t *area, const char *text,
    const lv_font_t *font, uint32_t color, lv_text_align_t align, uint8_t opacity,
    int32_t letter_space);
#endif
