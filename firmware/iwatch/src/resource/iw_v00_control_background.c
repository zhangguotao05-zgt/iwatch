#include "iw_v00_control_background.h"
#include <math.h>

enum { CORNER_RADIUS = 45 };

static float fade(float distance, float stop, float opacity)
{
    return distance < stop ? opacity * (1.0f - distance / stop) : 0.0f;
}

static uint8_t channel(float value)
{
    if (value <= 0.0f) return 0u;
    if (value >= 255.0f) return 255u;
    return (uint8_t)(value + 0.5f);
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(((uint16_t)(red >> 3u) << 11u) |
                      ((uint16_t)(green >> 2u) << 5u) |
                      (blue >> 3u));
}

static uint16_t rgb565_dithered(uint8_t red, uint8_t green, uint8_t blue,
                                unsigned x, unsigned y)
{
    static const uint8_t threshold[16] = {
        0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u,
        3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u
    };
    const unsigned offset = threshold[((y & 3u) << 2u) | (x & 3u)];
    const unsigned r = ((unsigned)red + (offset >> 1u)) >> 3u;
    const unsigned g = ((unsigned)green + (offset >> 2u)) >> 2u;
    const unsigned b = ((unsigned)blue + (offset >> 1u)) >> 3u;
    return (uint16_t)(((r > 31u ? 31u : r) << 11u) |
                      ((g > 63u ? 63u : g) << 5u) |
                      (b > 31u ? 31u : b));
}

static float rounded_coverage(float x, float y)
{
    const float right = (float)IW_V00_CONTROL_WIDTH - CORNER_RADIUS;
    const float bottom = (float)IW_V00_CONTROL_HEIGHT - CORNER_RADIUS;
    float dx = 0.0f, dy = 0.0f;
    if (x < CORNER_RADIUS) dx = x - CORNER_RADIUS;
    else if (x > right) dx = x - right;
    if (y < CORNER_RADIUS) dy = y - CORNER_RADIUS;
    else if (y > bottom) dy = y - bottom;
    if (dx == 0.0f || dy == 0.0f) return 1.0f;
    const float coverage = CORNER_RADIUS + 0.5f - sqrtf(dx * dx + dy * dy);
    if (coverage <= 0.0f) return 0.0f;
    return coverage >= 1.0f ? 1.0f : coverage;
}

bool iw_v00_control_background_line(uint16_t row, uint16_t *pixels, size_t capacity)
{
    if (!pixels || capacity < IW_V00_CONTROL_WIDTH || row >= IW_V00_CONTROL_HEIGHT)
        return false;

    const float y = (float)row + 0.5f;
    const float upper_y = y / (450.0f * 1.41421356f);
    const float lower_y = (y - 427.5f) / (427.5f * 1.41421356f);
    for (unsigned col = 0; col < IW_V00_CONTROL_WIDTH; ++col) {
        const float x = (float)col + 0.5f;
        const float coverage = rounded_coverage(x, y);
        if (coverage == 0.0f) {
            pixels[col] = rgb565(16u, 17u, 19u);
            continue;
        }
        const float lower_x = (x - 19.5f) / (370.5f * 1.41421356f);
        const float upper_x = (x - 390.0f) / (390.0f * 1.41421356f);
        const float brown = fade(sqrtf(lower_x * lower_x + lower_y * lower_y),
                                 0.65f, 0.61f);
        const float blue = fade(sqrtf(upper_x * upper_x + upper_y * upper_y),
                                0.62f, 0.60f);
        const float red = (22.0f * (1.0f - brown) + 116.0f * brown) *
                          (1.0f - blue) + 41.0f * blue;
        const float green = (23.0f * (1.0f - brown) + 100.0f * brown) *
                            (1.0f - blue) + 69.0f * blue;
        const float blue_channel = (27.0f * (1.0f - brown) + 69.0f * brown) *
                                   (1.0f - blue) + 92.0f * blue;
        pixels[col] = rgb565_dithered(
            channel(16.0f * (1.0f - coverage) + red * coverage),
            channel(17.0f * (1.0f - coverage) + green * coverage),
            channel(19.0f * (1.0f - coverage) + blue_channel * coverage),
            col, row);
    }
    return true;
}
