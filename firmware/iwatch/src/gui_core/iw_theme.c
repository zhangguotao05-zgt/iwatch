#include "iw_theme.h"
#include <stddef.h>

/* 390×450 工程初值，来自视觉规范；不是原厂内部参数或毫米尺寸。 */
static const iw_theme_t theme = {
    .width = 390, .height = 450, .inset_x = 18, .inset_y = 24,
    .gap = {4,8,12,16,24}, .row_min_height = 60, .card_radius = 24, .card_padding = 16,
    .button_height = 60, .button_radius = 30, .icon_size = 48, .hit_size = 56,
    .font_px = {20,22,26,30,64,80,96},
    .background = 0x000000, .surface = 0x17181C, .raised = 0x25262D,
    .text = 0xFFFFFF, .secondary = 0xB9BBC6, .accent = 0x0A84FF,
    .success = 0x30D158, .warning = 0xFFD60A, .danger = 0xFF453A
};

/* 降档和减弱动态效果只改变材质、动画，几何与语义保持一致。 */
#define NORMAL_MOTION .press_ms=75, .navigation_ms=250, .app_transition_ms=290, \
    .sheet_ms=250, .snap_ms=190, .value_ms=250, .toggle_ms=140, .pressed_scale_permille=970
static const iw_theme_effects_t effects[IW_THEME_QUALITY_COUNT][2] = {
    [IW_THEME_Q0] = {
        {.surface_opacity=255, .border_opacity=64, .highlight_opacity=24, NORMAL_MOTION},
        {.surface_opacity=255, .border_opacity=64, .highlight_opacity=24, .pressed_scale_permille=1000}
    },
    [IW_THEME_Q1] = {
        {.surface_opacity=224, .border_opacity=64, .highlight_opacity=40, .highlight_gradient=true, NORMAL_MOTION},
        {.surface_opacity=224, .border_opacity=64, .highlight_opacity=40, .highlight_gradient=true, .pressed_scale_permille=1000}
    }
};
#undef NORMAL_MOTION

const iw_theme_t *iw_theme_get(void)
{
    return &theme;
}

const iw_theme_effects_t *iw_theme_effects(iw_theme_quality_t quality, bool reduced_motion)
{
    return (unsigned)quality < IW_THEME_QUALITY_COUNT ? &effects[quality][reduced_motion ? 1 : 0] : NULL;
}

uint16_t iw_theme_font_px(iw_theme_font_role_t role)
{
    return (unsigned)role < IW_THEME_FONT_COUNT ? theme.font_px[role] : 0;
}
