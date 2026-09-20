#ifndef IW_PRODUCT_VIEW_H
#define IW_PRODUCT_VIEW_H
#include "iw_components.h"
#include "iw_product_scene.h"
#include "iw_font.h"

typedef void (*iw_product_action_fn)(uint16_t action, int32_t value, bool final, void *context);
typedef struct {
    iw_component_t frame;
    lv_obj_t *surface;
    iw_font_ref_t fonts[5];
    iw_product_scene_t scene;
    iw_product_action_fn action;
    void *context;
    int16_t scroll_y, press_x, last_x, press_y, last_y, pressed, feedback_node;
    uint16_t press_value, press_from, press_to;
    uint32_t press_started;
    int16_t picker_offset, picker_from, picker_parent_scroll;
    uint32_t picker_started, resumed_ms;
    iw_theme_quality_t quality;
    bool active, dragging, animating, reduced_motion, picker_snapping, first_draw;
} iw_product_view_t;
/* 句柄地址固定，零初始化；父屏幕删除时自动清空并释放字体引用。 */
bool iw_product_view_create(iw_product_view_t *view, lv_obj_t *parent, uint16_t page_id,
                            const iw_product_model_t *model, iw_product_action_fn action,
                            void (*quiesce)(void *), void *context);
bool iw_product_view_update(iw_product_view_t *view, const iw_product_model_t *model);
bool iw_product_view_destroy(iw_product_view_t *view);
void iw_product_view_activate(iw_product_view_t *view, bool active);
void iw_product_view_scroll(iw_product_view_t *view, int32_t delta);
uint32_t iw_product_view_tick(iw_product_view_t *view);
#endif
