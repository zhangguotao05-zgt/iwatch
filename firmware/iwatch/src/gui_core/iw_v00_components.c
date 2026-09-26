#include "iw_v00_components.h"
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
#include <string.h>

static iw_product_node_t *next_node(iw_product_scene_t *scene, int x, int y,
                                     int width, int height, bool fixed)
{
    if (!scene || x < 0 || y < 0 || x > 390 || width <= 0 ||
        width > 390 - x || height <= 0 || height > INT16_MAX ||
        y > INT16_MAX - height ||
        scene->count >= IW_PRODUCT_NODES) return NULL;
    iw_product_node_t *node = &scene->nodes[scene->count++];
    *node = (iw_product_node_t){.x = (int16_t)x, .y = (int16_t)y,
                                .width = (int16_t)width, .height = (int16_t)height,
                                .fixed = fixed};
    if (!fixed && y + height > scene->content_height)
        scene->content_height = (uint16_t)(y + height);
    return node;
}

bool iw_v00_rect(iw_product_scene_t *scene, int x, int y, int width, int height,
                 uint32_t fill, unsigned radius, uint16_t action, bool fixed)
{
    if (radius > 255u) return false;
    iw_product_node_t *node = next_node(scene, x, y, width, height, fixed);
    if (!node) return false;
    node->fill = fill;
    node->radius = (uint8_t)radius;
    node->action = action;
    return true;
}

bool iw_v00_gradient_rect(iw_product_scene_t *scene, int x, int y, int width,
                          int height, uint32_t start_color, uint32_t end_color,
                          uint8_t opacity, unsigned radius, uint16_t action,
                          bool fixed)
{
    if (!opacity || !iw_v00_rect(scene, x, y, width, height, start_color,
                                  radius, action, fixed)) return false;
    scene->v00_materials[scene->count - 1u] =
        (iw_v00_material_t){.end_color = end_color, .opacity = opacity,
                            .gradient = true};
    return true;
}

bool iw_v00_circle(iw_product_scene_t *scene, int cx, int cy, int radius,
                   uint32_t fill, uint16_t action, bool fixed)
{
    if (radius <= 0) return false;
    return iw_v00_rect(scene, cx - radius, cy - radius, radius * 2,
                       radius * 2, fill, (unsigned)radius, action, fixed);
}

bool iw_v00_painted_rect(iw_product_scene_t *scene, int x, int y, unsigned paint,
                         bool fixed)
{
    const iw_v00_paint_spec_t *spec = iw_v00_paint_spec(paint);
    if (!spec || !iw_v00_rect(scene, x, y, spec->width, spec->height,
                              spec->start_color, (spec->radius_half_px + 1u) / 2u,
                              0, fixed)) return false;
    scene->v00_materials[scene->count - 1u].paint = (uint8_t)paint;
    return true;
}

bool iw_v00_text(iw_product_scene_t *scene, int x, int y, int width, int height,
                 int baseline, unsigned font_px, uint32_t color, unsigned align,
                 const char *text, bool fixed)
{
    if (!text || strlen(text) >= IW_PRODUCT_TEXT_BYTES || font_px > 255u || align > 2u)
        return false;
    iw_product_node_t *node = next_node(scene, x, y, width, height, fixed);
    if (!node) return false;
    node->baseline = (int16_t)baseline;
    node->font_px = (uint8_t)font_px;
    node->color = color;
    node->align = (uint8_t)align;
    memcpy(node->text, text, strlen(text) + 1u);
    return true;
}

bool iw_v00_icon(iw_product_scene_t *scene, int x, int y, int size,
                 unsigned icon, uint32_t color, bool fixed)
{
    if (icon > 255u) return false;
    iw_product_node_t *node = next_node(scene, x, y, size, size, fixed);
    if (!node) return false;
    node->icon = (uint8_t)icon;
    node->color = color;
    return true;
}

bool iw_v00_asset(iw_product_scene_t *scene, int x, int y, int width, int height,
                  unsigned asset_index, uint16_t action, bool fixed)
{
    if (asset_index >= IW_ICON_V00_ASSET_COUNT) return false;
    iw_product_node_t *node = next_node(scene, x, y, width, height, fixed);
    if (!node) return false;
    node->icon = (uint8_t)(IW_ICON_V00_APP_FIRST + asset_index);
    node->action = action;
    return true;
}
#endif
