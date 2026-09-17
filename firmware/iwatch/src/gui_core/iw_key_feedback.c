#include "iw_key_feedback.h"
#include "lvgl.h"

static lv_obj_t *indicators[2];

bool iw_key_feedback_init(void)
{
    if (indicators[0]) return true;
    for (unsigned i = 0; i < 2; i++) {
        indicators[i] = lv_obj_create(lv_layer_top());
        if (!indicators[i]) {
            if (indicators[0]) lv_obj_delete(indicators[0]);
            indicators[0] = NULL;
            return false;
        }
        lv_obj_remove_style_all(indicators[i]);
        lv_obj_clear_flag(indicators[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(indicators[i], 14, 4);
        lv_obj_align(indicators[i], LV_ALIGN_TOP_MID, i ? 10 : -10, 6);
        lv_obj_set_style_bg_color(indicators[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(indicators[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(indicators[i], LV_OBJ_FLAG_HIDDEN);
    }
    return true;
}

void iw_key_feedback_set(unsigned key, bool pressed)
{
    if (key >= 2 || !indicators[key]) return;
    if (pressed) {
        lv_obj_move_foreground(indicators[key]);
        lv_obj_remove_flag(indicators[key], LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(indicators[key], LV_OBJ_FLAG_HIDDEN);
}

void iw_key_feedback_cancel(void)
{
    for (unsigned i = 0; i < 2; i++) iw_key_feedback_set(i, false);
}
