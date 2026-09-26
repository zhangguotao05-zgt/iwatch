#ifndef IW_V00_COMPONENTS_H
#define IW_V00_COMPONENTS_H
#include "iw_v00_paint.h"

#include "iw_product_scene.h"

/* V00 样片和后续页面共用固定容量节点；失败不留下半个节点。 */
bool iw_v00_rect(iw_product_scene_t *scene, int x, int y, int width, int height,
                 uint32_t fill, unsigned radius, uint16_t action, bool fixed);
bool iw_v00_gradient_rect(iw_product_scene_t *scene, int x, int y, int width,
                          int height, uint32_t start_color, uint32_t end_color,
                          uint8_t opacity, unsigned radius, uint16_t action,
                          bool fixed);
bool iw_v00_painted_rect(iw_product_scene_t *scene, int x, int y, unsigned paint,
                         bool fixed);
bool iw_v00_circle(iw_product_scene_t *scene, int cx, int cy, int radius,
                   uint32_t fill, uint16_t action, bool fixed);
bool iw_v00_text(iw_product_scene_t *scene, int x, int y, int width, int height,
                 int baseline, unsigned font_px, uint32_t color, unsigned align,
                 const char *text, bool fixed);
bool iw_v00_icon(iw_product_scene_t *scene, int x, int y, int size,
                 unsigned icon, uint32_t color, bool fixed);
bool iw_v00_asset(iw_product_scene_t *scene, int x, int y, int width, int height,
                  unsigned asset_index, uint16_t action, bool fixed);

#endif
