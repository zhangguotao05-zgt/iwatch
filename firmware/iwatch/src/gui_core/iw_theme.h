#ifndef IW_THEME_H
#define IW_THEME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { IW_THEME_Q0, IW_THEME_Q1, IW_THEME_QUALITY_COUNT } iw_theme_quality_t;

/* IW-VISUAL-2.0 产品页增量，不改写已验收的旧组件默认配色。 */
enum {
    IW_PRODUCT_WHITE = 0xffffff, IW_PRODUCT_SECONDARY = 0xb9bbc6,
    IW_PRODUCT_SURFACE = 0x242426, IW_PRODUCT_PRESSED = 0x343438,
    IW_PRODUCT_DISABLED_SURFACE = 0x303136,
    IW_PRODUCT_BLUE = 0x0a84ff, IW_PRODUCT_WARNING = 0xff9f0a,
    IW_PRODUCT_DISABLED = 0x91939d
};
typedef enum {
    IW_THEME_FONT_CAPTION, IW_THEME_FONT_SECONDARY, IW_THEME_FONT_BODY, IW_THEME_FONT_TITLE,
    IW_THEME_FONT_NUMBER_SMALL, IW_THEME_FONT_NUMBER_MEDIUM, IW_THEME_FONT_NUMBER_LARGE,
    IW_THEME_FONT_COUNT
} iw_theme_font_role_t;

typedef struct {
    uint16_t width, height, inset_x, inset_y;
    uint16_t gap[5];
    uint16_t row_min_height, card_radius, card_padding;
    uint16_t button_height, button_radius, icon_size, hit_size;
    uint16_t font_px[IW_THEME_FONT_COUNT];
    uint32_t background, surface, raised, text, secondary, accent, success, warning, danger;
} iw_theme_t;

typedef struct {
    uint8_t surface_opacity, border_opacity, highlight_opacity;
    uint16_t press_ms, navigation_ms, app_transition_ms, sheet_ms, snap_ms, value_ms, toggle_ms;
    uint16_t pressed_scale_permille;
    bool highlight_gradient;
} iw_theme_effects_t;

/* 全部指针指向静态只读数据；Q0/Q1 共享几何、字号和命中区。 */
const iw_theme_t *iw_theme_get(void);
const iw_theme_effects_t *iw_theme_effects(iw_theme_quality_t quality, bool reduced_motion);
/* 非法 role 返回 0，禁止悄悄使用另一个字号。 */
uint16_t iw_theme_font_px(iw_theme_font_role_t role);

#endif
