/* SPDX-License-Identifier: Apache-2.0 */
/* 蜂窝变换依据 SiFli SDK watch_v9/app_mainmenu.c 适配；目标端链接原 SDK 库。 */
#include "iw_cellular_layout.h"
#include "iw_routes.h"
#include "cell_transform.h"

enum {
    SCREEN_W = 390, SCREEN_H = 450,
    OUTER_RADIUS = 49, ICON_GAP = 6,
    ROUND_LIMIT = SCREEN_H / 2
};

static const iw_cellular_icon_t cells[IW_CELLULAR_ICON_COUNT] = {
    {79, 29, 64, 0}, {160, 23, 71, 0}, {247, 29, 64, 0},
    {32, 108, 65, 0}, {104, 94, 83, 0}, {203, 97, 83, 0},
    {294, 111, 65, IW_PAGE_SETTINGS}, {57, 188, 80, IW_PAGE_TIMER_LIST},
    {150, 183, 91, 0}, {252, 188, 80, IW_PAGE_STOPWATCH},
    {26, 283, 67, 0}, {104, 279, 79, 0}, {201, 279, 79, 0},
    {290, 282, 67, IW_PAGE_ALARM_LIST}, {70, 364, 64, 0},
    {156, 355, 78, 0}, {257, 362, 64, 0}
};

/* SDK 示例采用 60° 六角晶格；行距约 85 px，邻接中心距约 98 px。 */
static const int16_t lattice[IW_CELLULAR_ICON_COUNT][2] = {
    {97, 55}, {195, 55}, {293, 55},
    {48, 140}, {146, 140}, {244, 140}, {342, 140},
    {97, 225}, {195, 225}, {293, 225},
    {48, 310}, {146, 310}, {244, 310}, {342, 310},
    {97, 395}, {195, 395}, {293, 395}
};

static float smaller(float a, float b) { return a < b ? a : b; }
static float larger(float a, float b) { return a > b ? a : b; }

/* 与 watch_v9 的圆角边界处理一致：边缘图标沿圆弧内收并同步缩小。 */
static void limit_round(float *x, float *y, float *radius, float distance)
{
    if (distance + *radius <= ROUND_LIMIT) return;
    if (distance - *radius >= ROUND_LIMIT || distance <= 0.0f) {
        *radius = 0.0f;
        return;
    }
    float next_radius = (ROUND_LIMIT - distance + *radius) / 2.0f;
    float next_distance = ROUND_LIMIT - next_radius;
    float ratio = next_distance / distance;
    *x = SCREEN_W / 2.0f + (*x - SCREEN_W / 2.0f) * ratio;
    *y = SCREEN_H / 2.0f + (*y - SCREEN_H / 2.0f) * ratio;
    *radius = next_radius;
}

/* 对应 SDK 示例的 8/10 px 安全矩形裁剪，不让图标中心移出屏幕。 */
static bool limit_square(float *x, float *y, float *radius)
{
    float left = larger(8.0f, *x - *radius);
    float top = larger(10.0f, *y - *radius);
    float right = smaller(SCREEN_W - 9.0f, *x + *radius);
    float bottom = smaller(SCREEN_H - 11.0f, *y + *radius);
    if (left > right || top > bottom) return false;
    float width = right - left + 1.0f;
    float height = bottom - top + 1.0f;
    *radius = smaller(width, height) / 2.0f;
    *x = left + width / 2.0f;
    *y = top + height / 2.0f;
    return true;
}

static bool transformed_icon(unsigned index, int pan_x, int pan_y, int zoom,
                             iw_cellular_icon_t *icon)
{
    *icon = cells[index];
    if (zoom < -30) zoom = -30;
    if (zoom > 30) zoom = 30;
    float scale = 1.0f + zoom / 100.0f;
    float x = (float)(lattice[index][0] + pan_x);
    float y = (float)(lattice[index][1] + pan_y);
    float radius = OUTER_RADIUS;
    if (scale < 1.0f) {
        float eased = scale * scale;
        x = SCREEN_W / 2.0f + (x - SCREEN_W / 2.0f) * eased;
        y = SCREEN_H / 2.0f + (y - SCREEN_H / 2.0f) * eased;
        radius *= eased;
    }
    float before_x = x, before_y = y, before_radius = radius;
    float distance = 0.0f;
    if (!get_icon_transform_param(x, y, radius, &x, &y, &radius, &distance,
                                  SCREEN_W, SCREEN_H)) return false;
    limit_round(&x, &y, &radius, distance);
    if (!limit_square(&x, &y, &radius)) return false;
    radius = radius > ICON_GAP ? radius - ICON_GAP : 0.0f;
    if (scale < 1.0f) {
        x = before_x + (x - before_x) * scale;
        y = before_y + (y - before_y) * scale;
        radius = before_radius + (radius - before_radius) * scale;
    } else if (scale > 1.0f) {
        float eased = 1.0f / (2.0f - scale);
        x = SCREEN_W / 2.0f + (x - SCREEN_W / 2.0f) * eased;
        y = SCREEN_H / 2.0f + (y - SCREEN_H / 2.0f) * eased;
        radius *= eased;
    }
    if (radius < 1.0f || x < -128.0f || x > SCREEN_W + 128.0f ||
        y < -128.0f || y > SCREEN_H + 128.0f) return false;
    int size = (int)(radius * 2.0f + 0.5f);
    if (size > 160) size = 160;
    icon->size = (uint8_t)size;
    icon->x = (int16_t)(x - size / 2.0f + 0.5f);
    icon->y = (int16_t)(y - size / 2.0f + 0.5f);
    return true;
}

/* 用 SDK 相对零状态的位移与缩放驱动批准稿，避免首次显示突然改版。 */
static bool approved_icon(unsigned index, int pan_x, int pan_y, int zoom,
                          iw_cellular_icon_t *icon)
{
    if (!pan_x && !pan_y && !zoom) {
        *icon = cells[index];
        return true;
    }
    iw_cellular_icon_t origin;
    if (!transformed_icon(index, 0, 0, 0, &origin) ||
        !transformed_icon(index, pan_x, pan_y, zoom, icon)) return false;
    int x = cells[index].x + icon->x - origin.x;
    int y = cells[index].y + icon->y - origin.y;
    int size = (int)cells[index].size + icon->size - origin.size;
    if (size <= 0 || x + size < 0 || y + size < 0 ||
        x >= SCREEN_W || y >= SCREEN_H) return false;
    if (size > 160) size = 160;
    if (x < 8 || y < 10 || x + size > SCREEN_W - 9 ||
        y + size > SCREEN_H - 11) {
        float center_x = x + size / 2.0f, center_y = y + size / 2.0f;
        float radius = size / 2.0f;
        if (!limit_square(&center_x, &center_y, &radius)) return false;
        size = (int)(radius * 2.0f + 0.5f);
        x = (int)(center_x - size / 2.0f + 0.5f);
        y = (int)(center_y - size / 2.0f + 0.5f);
    }
    if (size <= 0) return false;
    icon->x = (int16_t)x;
    icon->y = (int16_t)y;
    icon->size = (uint8_t)size;
    return true;
}

static unsigned integer_sqrt(unsigned value)
{
    unsigned root = 0, bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else root >>= 1;
        bit >>= 2;
    }
    return root;
}

bool iw_cellular_layout(int pan_x, int pan_y, int zoom, bool runtime,
                        iw_cellular_icon_t icons[IW_CELLULAR_ICON_COUNT])
{
    if (!icons) return false;
    for (unsigned i = 0; i < IW_CELLULAR_ICON_COUNT; ++i) {
        icons[i] = cells[i];
        if (runtime && !approved_icon(i, pan_x, pan_y, zoom, &icons[i]))
            icons[i].size = 0;
    }
    if (!runtime) return true;
    unsigned factor = 1000u;
    for (unsigned i = 0; i < IW_CELLULAR_ICON_COUNT; ++i) {
        if (!icons[i].size) continue;
        for (unsigned j = i + 1; j < IW_CELLULAR_ICON_COUNT; ++j) {
            if (!icons[j].size) continue;
            int dx = 2 * icons[i].x + icons[i].size -
                     2 * icons[j].x - icons[j].size;
            int dy = 2 * icons[i].y + icons[i].size -
                     2 * icons[j].y - icons[j].size;
            unsigned distance2 = (unsigned)(dx * dx + dy * dy);
            unsigned required = icons[i].size + icons[j].size + 6u;
            if (distance2 >= required * required) continue;
            unsigned candidate = integer_sqrt(distance2) * 1000u / required;
            if (candidate < factor) factor = candidate;
        }
    }
    if (factor == 1000u) return true;
    /* 窄边界下整组等比例缩小，保持 SDK 的中心大小关系而不让单个图标突变。 */
    for (unsigned i = 0; i < IW_CELLULAR_ICON_COUNT; ++i) {
        iw_cellular_icon_t *icon = &icons[i];
        if (!icon->size) continue;
        int center_x = icon->x + icon->size / 2;
        int center_y = icon->y + icon->size / 2;
        unsigned size = icon->size * factor / 1000u;
        if (size > 1u) --size;
        icon->size = (uint8_t)size;
        icon->x = (int16_t)(center_x - (int)size / 2);
        icon->y = (int16_t)(center_y - (int)size / 2);
    }
    return true;
}
