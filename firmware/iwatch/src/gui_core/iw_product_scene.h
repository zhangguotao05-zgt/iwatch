#ifndef IW_PRODUCT_SCENE_H
#define IW_PRODUCT_SCENE_H
#include "iw_settings_model.h"
#include "iw_service.h"
#include "iw_routes.h"
#include "iw_product_text.h"
#include "iw_theme.h"

#define IW_PRODUCT_NODES 48u
#define IW_PRODUCT_TEXT_BYTES 128u
enum {
    IW_ICON_NONE,
    IW_ICON_BACK,
    IW_ICON_NEXT,
    IW_ICON_SUN,
    IW_ICON_CLOCK,
    IW_ICON_INFO,
    IW_ICON_SETTINGS
};
enum {
    IW_ACTION_BACK = 0x1000,
    IW_ACTION_FIELD,
    IW_ACTION_PICK_PREV = 0x1010,
    IW_ACTION_PICK_NEXT,
    IW_ACTION_PICK_CANCEL,
    IW_ACTION_PICK_CHOOSE,
    IW_ACTION_TIME_CANCEL,
    IW_ACTION_TIME_SAVE,
    IW_ACTION_DIM,
    IW_ACTION_BRIGHTEN,
    IW_ACTION_TRACK,
    IW_ACTION_RELOAD
};
typedef struct {
    int16_t x, y, width, height, baseline;
    uint16_t action;
    uint8_t font_px, radius, align, icon;
    bool fixed, disabled, multiline;
    uint32_t color, fill;
    char text[IW_PRODUCT_TEXT_BYTES];
} iw_product_node_t;
typedef struct {
    iw_product_node_t nodes[IW_PRODUCT_NODES];
    uint16_t page_id, count, content_height, clip_top, clip_bottom;
} iw_product_scene_t;
typedef struct {
    iw_clock_snapshot_t clock;
    iw_brightness_snapshot_t brightness;
    iw_time_draft_t draft;
    const char *hardware, *firmware, *toolchain;
    iw_product_text_id_t message;
    uint8_t preview_level;
    iw_theme_quality_t quality;
    bool back, large_text, pending, display_available, time_available, reduced_motion;
} iw_product_model_t;
/* 固定容量；全部字符串复制。输出不引用页面草稿或临时格式化缓冲。 */
bool iw_product_scene_build(iw_product_scene_t *scene, uint16_t page_id, const iw_product_model_t *model);
int iw_product_scene_hit(const iw_product_scene_t *scene, int x, int y, int scroll_y);
int iw_product_scene_scroll_limit(const iw_product_scene_t *scene);
#endif
