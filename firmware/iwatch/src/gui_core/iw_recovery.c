#include "iw_recovery.h"
#include "lvgl.h"
#include <string.h>

static lv_obj_t *recovery_panel;
static lv_obj_t *recovery_label;
static const char *recovery_owner;

bool iw_recovery_init(void)
{
    if (recovery_panel) return true;
    recovery_panel = lv_obj_create(lv_layer_top());
    if (!recovery_panel) return false;
    lv_obj_remove_style_all(recovery_panel);
    lv_obj_set_size(recovery_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(recovery_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(recovery_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(recovery_panel, LV_OBJ_FLAG_SCROLLABLE);

    recovery_label = lv_label_create(recovery_panel);
    if (!recovery_label)
    {
        lv_obj_delete(recovery_panel);
        recovery_panel = NULL;
        return false;
    }
    /* 最小恢复层只用已内置的拉丁字库，不加载完整中文字体或外部资源。 */
    lv_obj_set_style_text_font(recovery_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(recovery_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(recovery_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(recovery_label, LV_PCT(85));
    lv_label_set_text_static(recovery_label, "UI unavailable\nPress Crown to return");
    lv_obj_center(recovery_label);
    lv_obj_add_flag(recovery_panel, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void iw_recovery_show(const char *owner)
{
    /* 失败路径不创建控件、不替换文本，避免内存不足后再次申请资源。 */
    recovery_owner = owner;
    if (recovery_panel)
    {
        lv_obj_remove_flag(recovery_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(recovery_panel);
    }
}

void iw_recovery_hide(const char *owner)
{
    if (recovery_panel && recovery_owner && owner && strcmp(owner, recovery_owner) == 0)
    {
        lv_obj_add_flag(recovery_panel, LV_OBJ_FLAG_HIDDEN);
        recovery_owner = NULL;
    }
}
