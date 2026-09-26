#include "iw_v00_paint.h"

static const iw_v00_paint_spec_t specs[] = {
    {147, 147, 147, 150, 155, 155, 4, 0x303033, 0x222225, 0, 255, 0},
    {350, 96, 48, 120, 350, 96, 0, 0x2b2b2e, 0x242426, 0, 255, 0},
    {350, 98, 48, 120, 350, 98, 0, 0x29292c, 0x202022, 0, 255, 0},
    /* 原规格 e007/009/011/013/015/017；CSS 54px 半径按高度收缩到 51.5px。 */
    {167, 103, 103, 150, 167, 103, 0, 0x767776, 0x454448, 0xb8bec7, 117, 56},
    {167, 103, 103, 130, 167, 103, 0, 0x00bde9, 0x0989fb, 0x41d7fb, 255, 255},
    {167, 103, 103, 140, 167, 103, 0, 0x7860ff, 0x5d40fa, 0x9c8cff, 255, 255}
};

const iw_v00_paint_spec_t *iw_v00_paint_spec(unsigned id)
{
    return id > IW_V00_PAINT_NONE && id < IW_V00_PAINT_COUNT ? &specs[id - 1u] : NULL;
}

static uint8_t rounded_coverage(int width, int height, int radius_half_px, int x, int y)
{
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    const int radius = radius_half_px * 4;
    const int right = width * 8 - radius;
    const int bottom = height * 8 - radius;
    const int left_x = x * 8, top_y = y * 8;
    if ((left_x >= radius && left_x + 8 <= right) ||
        (top_y >= radius && top_y + 8 <= bottom)) return 255;
    unsigned inside = 0;
    /* 4×4 覆盖采样仅用于圆角边缘；内部直接全覆盖。 */
    for (int oy = 1; oy < 8; oy += 2) {
        for (int ox = 1; ox < 8; ox += 2) {
            int px = left_x + ox, py = top_y + oy;
            int dx = px < radius ? radius - px : px > right ? px - right : 0;
            int dy = py < radius ? radius - py : py > bottom ? py - bottom : 0;
            if (dx * dx + dy * dy <= radius * radius) ++inside;
        }
    }
    return (uint8_t)((inside * 255u + 8u) / 16u);
}

static unsigned inset_highlight(int x, int y)
{
    /* 2px blur 的有限核近似，1px 向下偏移；只在内边界附近计算。 */
    if (rounded_coverage(157, 93, 93, x - 5, y - 5) == 255) return 0;
    static const unsigned weights[] = {1, 4, 6, 4, 1};
    unsigned covered = 0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            covered += weights[dx + 2] * weights[dy + 2] *
                rounded_coverage(163, 99, 99, x - 2 + dx, y - 3 + dy);
    return ((255u - ((covered + 128u) / 256u)) * 34u + 127u) / 255u;
}

static unsigned over_channel(unsigned below, unsigned below_alpha, unsigned above,
                             unsigned above_alpha, unsigned alpha_numerator)
{
    if (!alpha_numerator) return 0;
    return (above * above_alpha * 255u + below * below_alpha * (255u - above_alpha) +
            alpha_numerator / 2u) / alpha_numerator;
}

static unsigned composite(unsigned *red, unsigned *green, unsigned *blue,
                          unsigned alpha, uint32_t color, unsigned overlay_alpha)
{
    unsigned numerator = overlay_alpha * 255u + alpha * (255u - overlay_alpha);
    *red = over_channel(*red, alpha, (color >> 16) & 255u, overlay_alpha, numerator);
    *green = over_channel(*green, alpha, (color >> 8) & 255u, overlay_alpha, numerator);
    *blue = over_channel(*blue, alpha, color & 255u, overlay_alpha, numerator);
    return (numerator + 127u) / 255u;
}

static unsigned gradient_channel(unsigned start, unsigned end, int32_t numerator, int32_t denominator)
{
    return (unsigned)(((int64_t)start * (denominator - numerator) +
                       (int64_t)end * numerator + denominator / 2) / denominator);
}

static unsigned quantize(unsigned value, unsigned levels, unsigned threshold)
{
    /* 有序量化保留暗部平均亮度，不修改原渐变端点或添加独立设计纹理。 */
    unsigned scaled = value * levels;
    unsigned result = scaled / 255u;
    if ((scaled % 255u) * 16u > threshold * 255u + 127u && result < levels) ++result;
    return result;
}

bool iw_v00_paint_pixels(unsigned id, uint8_t *pixels, size_t capacity)
{
    const iw_v00_paint_spec_t *s = iw_v00_paint_spec(id);
    if (!s || !pixels || capacity < (size_t)s->width * s->height * 3u) return false;
    /* CSS 角度从上方顺时针，限定不可变规格的 Q15 方向，不在绘制时求三角函数。 */
    int32_t dx, dy;
    switch (s->angle_deg) {
    case 120: dx = 28378; dy = 16384; break;
    case 130: dx = 25102; dy = 21063; break;
    case 140: dx = 21063; dy = 25102; break;
    case 150: dx = 16384; dy = 28378; break;
    default: return false;
    }
    const int32_t extent = (int32_t)s->gradient_width * dx + (int32_t)s->gradient_height * dy;
    const int32_t denominator = extent * 2;
    const size_t alpha_offset = (size_t)s->width * s->height * 2u;
    static const uint8_t bayer[] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    for (unsigned y = 0; y < s->height; ++y) {
        for (unsigned x = 0; x < s->width; ++x) {
            int32_t numerator = ((int32_t)(2u * (x + s->inset_px) + 1u) - s->gradient_width) * dx +
                                ((int32_t)(2u * (y + s->inset_px) + 1u) - s->gradient_height) * dy + extent;
            if (numerator < 0) numerator = 0;
            if (numerator > denominator) numerator = denominator;
            unsigned red = gradient_channel((s->start_color >> 16) & 255u, (s->end_color >> 16) & 255u, numerator, denominator);
            unsigned green = gradient_channel((s->start_color >> 8) & 255u, (s->end_color >> 8) & 255u, numerator, denominator);
            unsigned blue = gradient_channel(s->start_color & 255u, s->end_color & 255u, numerator, denominator);
            unsigned alpha = s->fill_opacity;
            unsigned outer = rounded_coverage(s->width, s->height, s->radius_half_px, (int)x, (int)y);
            if (id >= IW_V00_PAINT_CONTROL_GRAY && outer) {
                unsigned inner = rounded_coverage(163, 99, 99, (int)x - 2, (int)y - 2);
                /* 内阴影在填充之上，边框只占 2px 环带；不再叠整块半透明底。 */
                unsigned highlight = (inset_highlight((int)x, (int)y) * inner + outer / 2u) / outer;
                alpha = composite(&red, &green, &blue, alpha, 0xffffff, highlight);
                unsigned border = (s->border_opacity * (outer - inner) + outer / 2u) / outer;
                alpha = composite(&red, &green, &blue, alpha, s->border_color, border);
            }
            unsigned threshold = bayer[((y & 3u) << 2) | (x & 3u)];
            uint16_t color = (uint16_t)((quantize(red, 31u, threshold) << 11) |
                                       (quantize(green, 63u, threshold) << 5) |
                                       quantize(blue, 31u, threshold));
            size_t index = (size_t)y * s->width + x;
            pixels[index * 2u] = (uint8_t)color;
            pixels[index * 2u + 1u] = (uint8_t)(color >> 8);
            pixels[alpha_offset + index] = (uint8_t)((outer * alpha + 127u) / 255u);
        }
    }
    return true;
}
