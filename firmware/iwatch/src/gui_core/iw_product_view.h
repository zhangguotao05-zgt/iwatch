#ifndef IW_PRODUCT_VIEW_H
#define IW_PRODUCT_VIEW_H
#include "iw_components.h"
#include "iw_product_scene.h"
#include "iw_font.h"
#include "iw_cellular_layout.h"

typedef void (*iw_product_action_fn)(uint16_t action, int32_t value, bool final, void *context);
typedef struct {
    iw_component_t frame;
    lv_obj_t *surface;
    iw_font_ref_t fonts[10];
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
    iw_font_ref_t v00_fonts[20];
    uint16_t v00_weights[20];
    uint8_t v00_sizes[20], v00_font_count, v00_paint_refs;
#endif
    iw_product_scene_t scene;
    iw_product_action_fn action;
    void *context;
    const lv_image_dsc_t *const *v00_images;
    uint8_t v00_image_count;
    bool control_cache_owned;
    lv_obj_t *cellular_icons[IW_CELLULAR_ICON_COUNT];
    iw_cellular_icon_t cellular_bounds[IW_CELLULAR_ICON_COUNT];
    int16_t cellular_pan_x, cellular_pan_y;
    int8_t cellular_zoom;
    int16_t scroll_y, press_x, last_x, press_y, last_y, pressed, feedback_node;
    uint16_t press_value, press_from, press_to;
    uint32_t press_started;
    int16_t picker_offset, picker_from, picker_parent_scroll;
    int8_t zoom_drag_remainder;
    int8_t alarm_wheel_field;
    int16_t alarm_wheel_remainder;
    uint32_t picker_started, resumed_ms, last_tap_ms;
    int16_t last_tap_x, last_tap_y;
    iw_theme_quality_t quality;
    bool active, dragging, animating, reduced_motion, picker_snapping, first_draw,
         face_long_press_fired, last_tap_valid, launcher_zooming;
} iw_product_view_t;
/* 句柄地址固定、零初始化；父删忙态保留缓存引用，调用者须等 idle 后 destroy 成功才回收句柄。 */
bool iw_product_view_create(iw_product_view_t *view, lv_obj_t *parent, uint16_t page_id,
                            const iw_product_model_t *model, iw_product_action_fn action,
                            void (*quiesce)(void *), void *context);
/* 本地视觉验收使用；素材由调用方持有，必须存活到绘制完成和页面销毁。 */
bool iw_product_view_set_v00_images(iw_product_view_t *view,
    const lv_image_dsc_t *const *images, unsigned count);
bool iw_product_view_update(iw_product_view_t *view, const iw_product_model_t *model);
bool iw_product_view_destroy(iw_product_view_t *view);
void iw_product_view_activate(iw_product_view_t *view, bool active);
void iw_product_view_scroll(iw_product_view_t *view, int32_t delta);
uint32_t iw_product_view_tick(iw_product_view_t *view);
#endif
