#ifndef IW_V00_CONTROL_BACKGROUND_H
#define IW_V00_CONTROL_BACKGROUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IW_V00_CONTROL_WIDTH 390u
#define IW_V00_CONTROL_HEIGHT 450u

/* 每次只生成一行 RGB565；调用方提供至少 390 个像素的缓冲区。 */
bool iw_v00_control_background_line(uint16_t row, uint16_t *pixels, size_t capacity);

#endif
