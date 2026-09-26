#include "iw_product_view.h"
#include "iw_draw_checked.h"
#include "iw_gui_owner.h"
#include "iw_font_port.h"
#include "iw_render_probe.h"
#include "iw_v00_scene.h"
#include "iw_v00_paint_cache.h"
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_event_private.h"
#include "src/core/lv_obj_class_private.h"
#include "src/core/lv_obj_style_private.h"
#include "src/draw/lv_draw_private.h"
#include "src/misc/lv_area_private.h"
#include "src/misc/lv_text_private.h"
#include <string.h>
#ifdef IW_TARGET_BUILD
#include "../resource/iw_v00_control_cache.h"
#include "../resource/images/iw_v00_resource_catalog.h"
#include "app_mem.h"
#endif

typedef struct {
    lv_obj_t base;
    iw_product_view_t *view;
    lv_style_t geometry;
    lv_style_const_prop_t props[5];
} product_surface_t;
static const uint8_t sizes[] = {20, 22, 24, 26, 28, 30, 32, 48, 64, 80};
#define PICKER_STEP_PX 75
#define PICKER_TOP 116
#define PICKER_BOTTOM 326
static void event(const lv_obj_class_t *class_p, lv_event_t *e);
static void destructor(const lv_obj_class_t *class_p, lv_obj_t *object);
static bool cellular_native(const iw_product_view_t *view);
static const lv_obj_class_t surface_class = {.base_class = &lv_obj_class,
                                             .event_cb = event,
                                             .destructor_cb = destructor,
                                             .instance_size = sizeof(product_surface_t),
                                             .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE,
                                             .name = "iw_product"};

static unsigned font_index(unsigned px) {
    for (unsigned i = 0; i < sizeof(sizes); i++)
        if (sizes[i] == px) return i;
    return sizeof(sizes);
}

#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
static unsigned fallback_font_index(unsigned px)
{
    unsigned nearest = 0u;
    unsigned distance = UINT32_MAX;
    for (unsigned i = 0; i < sizeof(sizes); ++i) {
        unsigned difference = sizes[i] > px ? sizes[i] - px : px - sizes[i];
        if (difference < distance) { nearest = i; distance = difference; }
    }
    return nearest;
}

static bool font_covers_text(const lv_font_t *font, const char *text)
{
    if (!font || !text) return false;
    uint32_t offset = 0;
    while (text[offset]) {
        uint32_t previous = offset;
        uint32_t codepoint = lv_text_encoded_next(text, &offset);
        lv_font_glyph_dsc_t glyph = {0};
        if (offset <= previous || !lv_font_get_glyph_dsc(font, &glyph, codepoint, 0))
            return false;
    }
    return true;
}

static bool is_v00_page(const iw_product_scene_t *scene)
{
    return scene->v00_style;
}

static const lv_font_t *v00_font(const iw_product_view_t *v, uint16_t weight, uint8_t size_px)
{
    for (unsigned i = 0; i < v->v00_font_count; ++i)
        if (v->v00_weights[i] == weight && v->v00_sizes[i] == size_px)
            return v->v00_fonts[i].font;
    return NULL;
}

static bool acquire_v00_font(iw_product_view_t *v, uint16_t weight, uint8_t size_px)
{
    if (v00_font(v, weight, size_px)) return true;
    if (v->v00_font_count >= sizeof(v->v00_fonts) / sizeof(v->v00_fonts[0])) return false;
    unsigned index = v->v00_font_count;
    if (iw_font_acquire_v00(weight, size_px, &v->v00_fonts[index]) != IW_FONT_OK)
        return false;
    v->v00_weights[index] = weight;
    v->v00_sizes[index] = size_px;
    ++v->v00_font_count;
    return true;
}
#endif

#ifdef IW_TARGET_BUILD
static iw_v00_control_cache_t control_cache;
static uint16_t control_references;
static lv_image_dsc_t control_background;
static const lv_image_dsc_t *control_images[IW_ICON_V00_ASSET_COUNT];

static bool prepare_foreground_images(iw_product_view_t *view)
{
    if (iw_v00_resource_catalog_count() != IW_ICON_V00_ASSET_COUNT - 1u) return false;
    for (unsigned i = 0; i < IW_ICON_V00_ASSET_COUNT - 1u; ++i) {
        if (iw_v00_resource_catalog_validate(i, false) != IW_V00_RESOURCE_OK)
            return false;
        control_images[i] = iw_v00_resource_catalog_image(i);
    }
    view->v00_images = control_images;
    view->v00_image_count = IW_ICON_V00_ASSET_COUNT - 1u;
    return true;
}

static bool control_asset_valid(void *context, unsigned index)
{
    (void)context;
    return iw_v00_resource_catalog_validate(index, false) == IW_V00_RESOURCE_OK;
}

static void *control_allocate(void *context, size_t bytes)
{
    (void)context;
    return app_cache_alloc(bytes, IMAGE_CACHE_PSRAM);
}

static void control_release(void *context, void *buffer)
{
    (void)context;
    app_cache_free(buffer);
}

static void control_flush(void *context, void *buffer, size_t bytes)
{
    (void)context;
    app_mem_flush_cache(buffer, (uint32_t)bytes);
}

static bool control_render_idle(void *context)
{
    (void)context;
    return iw_font_port_render_idle();
}

static bool prepare_control_images(iw_product_view_t *view)
{
    if (!iw_font_port_is_owner() || !iw_font_port_render_idle()) return false;
    if (view->control_cache_owned) return true;
    if (control_references == UINT16_MAX) return false;
    const iw_v00_control_cache_ops_t ops = {
        NULL, control_asset_valid, control_allocate, control_release,
        control_flush, control_render_idle
    };
    if (iw_v00_resource_catalog_count() != IW_ICON_V00_ASSET_COUNT - 1u ||
        !iw_v00_control_cache_prepare(&control_cache, &ops)) return false;
    for (unsigned i = 0; i < IW_ICON_V00_ASSET_COUNT - 1u; ++i)
        control_images[i] = iw_v00_resource_catalog_image(i);
    /* 已发布描述符由所有持有者共用，追加持有者不重写或重建像素。 */
    if (!control_references) {
        control_background.header.magic = LV_IMAGE_HEADER_MAGIC;
        control_background.header.cf = LV_COLOR_FORMAT_RGB565;
        control_background.header.w = IW_V00_CONTROL_WIDTH;
        control_background.header.h = IW_V00_CONTROL_HEIGHT;
        control_background.header.stride = IW_V00_CONTROL_WIDTH * 2u;
        control_background.data_size = IW_V00_CONTROL_BYTES;
        control_background.data = (const uint8_t *)control_cache.pixels;
    }
    ++control_references;
    view->control_cache_owned = true;
    control_images[IW_ICON_V00_ASSET_COUNT - 1u] = &control_background;
    view->v00_images = control_images;
    view->v00_image_count = IW_ICON_V00_ASSET_COUNT;
    return true;
}

static bool release_control_images(iw_product_view_t *view)
{
    if (!iw_font_port_is_owner()) return false;
    if (!view->control_cache_owned) return true;
    if (!control_references || !iw_font_port_render_idle()) return false;
    const iw_v00_control_cache_ops_t ops = {
        .release = control_release, .render_idle = control_render_idle
    };
    /* 只释放本 View 实际取得的引用；最后一份必须等绘制安全，失败保留全部状态。 */
    if (control_references == 1u) {
        if (!iw_v00_control_cache_release(&control_cache, &ops)) return false;
        control_background.data = NULL;
    }
    --control_references;
    view->control_cache_owned = false;
    view->v00_images = NULL;
    view->v00_image_count = 0;
    return true;
}
#endif

static void feedback(iw_product_view_t *v, bool down) {
    if (down) v->feedback_node = v->pressed;
    v->press_from = v->press_value;
    v->press_to = down ? 1000 : 0;
    v->press_started = lv_tick_get();
    const iw_theme_effects_t *effects = iw_theme_effects(v->quality, v->reduced_motion);
    v->animating = effects->press_ms && v->press_from != v->press_to;
    if (!v->animating) v->press_value = v->press_to;
}

/* 清除空白区双击候选，避免取消、拖动或跨页面输入残留。 */
static void clear_launcher_tap(iw_product_view_t *v) {
    v->last_tap_valid = false;
    v->last_tap_ms = 0;
    v->last_tap_x = v->last_tap_y = 0;
}

static void picker_snap(iw_product_view_t *v) {
    v->picker_from = v->picker_offset;
    v->picker_started = lv_tick_get();
    v->picker_snapping = v->picker_offset && !v->reduced_motion;
    if (!v->picker_snapping) v->picker_offset = 0;
}

uint32_t iw_product_view_tick(iw_product_view_t *v) {
    if (!v || !v->surface || !v->active || !iw_font_port_is_owner()) return UINT32_MAX;
    const iw_theme_effects_t *effects = iw_theme_effects(v->quality, v->reduced_motion);
    bool changed = v->animating || v->picker_snapping;
    if (v->animating) {
        uint32_t elapsed = lv_tick_get() - v->press_started;
        if (!effects->press_ms || elapsed >= effects->press_ms) {
            v->press_value = v->press_to;
            v->animating = false;
        } else {
            v->press_value = (uint16_t)(v->press_from +
                ((int32_t)v->press_to - v->press_from) * (int32_t)elapsed / effects->press_ms);
        }
    }
    if (v->picker_snapping) {
        uint32_t elapsed = lv_tick_get() - v->picker_started;
        if (!effects->snap_ms || elapsed >= effects->snap_ms) {
            v->picker_offset = 0;
            v->picker_snapping = false;
        } else {
            /* 剩余位移乘 (1-t)^3，定点运算不依赖浮点或动态定时器。 */
            int32_t left = (int32_t)(1024u * (effects->snap_ms - elapsed) / effects->snap_ms);
            v->picker_offset = (int16_t)(v->picker_from * left / 1024 * left / 1024 * left / 1024);
        }
    }
    if (changed && !cellular_native(v)) lv_obj_invalidate(v->surface);
    return v->animating || v->picker_snapping ? 16u : UINT32_MAX;
}

static void icon_line_width(lv_layer_t *layer, const lv_area_t *a, uint32_t color,
                            int x1, int y1, int x2, int y2, int width) {
    int size = lv_area_get_width(a);
    x1 = a->x1 + x1 * size / 48;
    x2 = a->x1 + x2 * size / 48;
    y1 = a->y1 + y1 * size / 48;
    y2 = a->y1 + y2 * size / 48;
    lv_area_t bounds = {LV_MIN(x1, x2) - width, LV_MIN(y1, y2) - width, LV_MAX(x1, x2) + width,
                        LV_MAX(y1, y2) + width};
    lv_draw_task_t *task = lv_draw_add_task_checked(layer, &bounds, LV_DRAW_TASK_TYPE_LINE);
    if (!task) {
        iw_gui_fault_raise();
        return;
    }
    lv_draw_line_dsc_t *d = task->draw_dsc;
    lv_draw_line_dsc_init(d);
    d->p1 = (lv_point_precise_t){x1, y1};
    d->p2 = (lv_point_precise_t){x2, y2};
    d->width = width;
    d->color = lv_color_hex(color);
    d->opa = 255;
    d->round_start = d->round_end = 1;
    d->allocation_failed_cb = iw_gui_fault_raise;
    lv_draw_finalize_task_creation(layer, task);
}

static void icon_line(lv_layer_t *layer, const lv_area_t *a, uint32_t color, int x1, int y1, int x2, int y2) {
    icon_line_width(layer, a, color, x1, y1, x2, y2, lv_area_get_width(a) >= 40 ? 3 : 2);
}

static void icon_ring(lv_layer_t *layer, const lv_area_t *a, uint32_t color, int radius) {
    int size = lv_area_get_width(a), r = radius * size / 48;
    int cx = a->x1 + size / 2, cy = a->y1 + size / 2;
    lv_area_t ring = {cx - r, cy - r, cx + r, cy + r};
    if (!iw_draw_fill_checked(layer, &ring, color, r, 255, false, 0)) return;
    int width = size >= 40 ? 3 : 2;
    ring.x1 += width;
    ring.x2 -= width;
    ring.y1 += width;
    ring.y2 -= width;
    (void)iw_draw_fill_checked(layer, &ring, 0x242426, r - width, 255, false, 0);
}

static void draw_icon(lv_layer_t *layer, const lv_area_t *a, unsigned icon, uint32_t color) {
    if (icon == IW_ICON_V00_DISPLAY_NEXT) {
        /* 仅显示设置两行：原交接向量轮廓，不能改变通用 NEXT 的命中或外观。 */
        icon_line_width(layer, a, color, 17, 12, 31, 24, 1);
        icon_line_width(layer, a, color, 31, 24, 17, 36, 1);
        return;
    }
    if (icon == IW_ICON_V00_CLOSE || icon == IW_ICON_V00_CHECK) {
        if (icon == IW_ICON_V00_CLOSE) {
            icon_line(layer, a, color, 14, 14, 34, 34);
            icon_line(layer, a, color, 34, 14, 14, 34);
        } else {
            icon_line(layer, a, color, 9, 25, 20, 35);
            icon_line(layer, a, color, 20, 35, 39, 12);
        }
        return;
    }
    if (icon == IW_ICON_V00_DIAL) {
        static const int ticks[][4] = {
            {24, 2, 24, 7}, {35, 5, 33, 10}, {43, 13, 38, 16},
            {46, 24, 41, 24}, {43, 35, 38, 32}, {35, 43, 33, 38},
            {24, 46, 24, 41}, {13, 43, 16, 38}, {5, 35, 10, 32},
            {2, 24, 7, 24}, {5, 13, 10, 16}, {13, 5, 16, 10}
        };
        for (unsigned i = 0; i < sizeof(ticks) / sizeof(ticks[0]) &&
             !iw_gui_fault_pending(); i++)
            icon_line(layer, a, color, ticks[i][0], ticks[i][1],
                      ticks[i][2], ticks[i][3]);
        return;
    }
    if (icon == IW_ICON_V00_MUSIC) {
        icon_line(layer, a, color, 23, 10, 37, 7);
        icon_line(layer, a, color, 23, 10, 23, 35);
        icon_line(layer, a, color, 37, 7, 37, 31);
        icon_ring(layer, a, color, 5);
        return;
    }
    if (icon == IW_ICON_V00_WIFI) {
        icon_line(layer, a, color, 9, 19, 24, 10);
        icon_line(layer, a, color, 24, 10, 39, 19);
        icon_line(layer, a, color, 16, 27, 24, 21);
        icon_line(layer, a, color, 24, 21, 32, 27);
        icon_line(layer, a, color, 23, 36, 25, 36);
        return;
    }
    if (icon == IW_ICON_V00_AIRPLANE) {
        icon_line(layer, a, color, 4, 25, 42, 19);
        icon_line(layer, a, color, 42, 19, 20, 31);
        icon_line(layer, a, color, 20, 31, 23, 43);
        icon_line(layer, a, color, 23, 43, 4, 25);
        return;
    }
    if (icon == IW_ICON_V00_MOON) {
        icon_ring(layer, a, color, 19);
        return;
    }
    if (icon == IW_ICON_BACK || icon == IW_ICON_NEXT) {
        int start = icon == IW_ICON_BACK ? 29 : 17, middle = icon == IW_ICON_BACK ? 17 : 31;
        icon_line(layer, a, color, start, 12, middle, 24);
        icon_line(layer, a, color, middle, 24, start, 36);
        return;
    }
    if (icon == IW_ICON_GRID) {
        /* 蜂窝入口使用 3×3 点阵，保持图标自绘且不引入图片资源。 */
        static const int points[][2] = {{15, 15}, {24, 15}, {33, 15},
                                        {15, 24}, {24, 24}, {33, 24},
                                        {15, 33}, {24, 33}, {33, 33}};
        int size = lv_area_get_width(a);
        int radius = size >= 40 ? 4 : 3;
        for (unsigned i = 0; i < sizeof(points) / sizeof(points[0]) && !iw_gui_fault_pending(); i++) {
            int cx = a->x1 + points[i][0] * size / 48;
            int cy = a->y1 + points[i][1] * size / 48;
            lv_area_t dot = {cx - radius, cy - radius, cx + radius, cy + radius};
            (void)iw_draw_fill_checked(layer, &dot, color, radius, 255, false, 0);
        }
        return;
    }
    icon_ring(layer, a, color, icon == IW_ICON_SUN ? 9 : icon == IW_ICON_SETTINGS ? 14 : 18);
    if (icon == IW_ICON_SETTINGS) icon_ring(layer, a, color, 6);
    if (icon == IW_ICON_SUN || icon == IW_ICON_SETTINGS) {
        static const int rays[][4] = {{24, 3, 24, 8}, {24, 40, 24, 45}, {3, 24, 8, 24},  {40, 24, 45, 24},
                                      {9, 9, 13, 13}, {35, 35, 39, 39}, {9, 39, 13, 35}, {35, 13, 39, 9}};
        for (unsigned i = 0; i < 8 && !iw_gui_fault_pending(); i++)
            icon_line(layer, a, color, rays[i][0], rays[i][1], rays[i][2], rays[i][3]);
    } else if (icon == IW_ICON_CLOCK || icon == IW_ICON_TIMER ||
               icon == IW_ICON_STOPWATCH || icon == IW_ICON_ALARM) {
        icon_line(layer, a, color, 24, 12, 24, 25);
        icon_line(layer, a, color, 24, 25, 33, 30);
        if (icon == IW_ICON_STOPWATCH || icon == IW_ICON_ALARM) {
            icon_line(layer, a, color, 18, 4, 30, 4);
            icon_line(layer, a, color, 24, 4, 24, 8);
        }
    } else if (icon == IW_ICON_INFO) {
        icon_line(layer, a, color, 24, 14, 24, 16);
        icon_line(layer, a, color, 24, 23, 24, 35);
    }
}

static bool draw_v00_image(iw_product_view_t *view, lv_layer_t *layer,
                           const lv_area_t *area, unsigned index, int16_t rotation)
{
    const lv_image_dsc_t *image = index < view->v00_image_count
        ? view->v00_images[index] : NULL;
    bool opaque = index == IW_ICON_V00_ASSET_COUNT - 1u;
    bool scaled_app = image && index < IW_ICON_V00_APP_COUNT &&
        ((int)image->header.w != lv_area_get_width(area) ||
         (int)image->header.h != lv_area_get_height(area));
    if (!image || image->header.magic != LV_IMAGE_HEADER_MAGIC ||
#ifdef IW_TARGET_BUILD
        (opaque ? image->header.cf != LV_COLOR_FORMAT_RGB565 ||
                  image->header.stride != IW_V00_CONTROL_WIDTH * 2u ||
                  image->data_size != IW_V00_CONTROL_BYTES :
                  image->header.cf != LV_COLOR_FORMAT_RAW_ALPHA ||
                  image->header.flags != LV_IMAGE_FLAGS_EZIP) ||
#else
        image->header.cf != (uint8_t)(opaque ? LV_COLOR_FORMAT_RGB565 :
                                               LV_COLOR_FORMAT_RGB565A8) ||
#endif
        (!scaled_app && ((int)image->header.w != lv_area_get_width(area) ||
                         (int)image->header.h != lv_area_get_height(area))) ||
        (scaled_app && (lv_area_get_width(area) < 32 ||
                        lv_area_get_width(area) > 160 ||
                        lv_area_get_width(area) != lv_area_get_height(area))) ||
#ifndef IW_TARGET_BUILD
        image->header.stride != image->header.w * 2u ||
        image->data_size != (uint32_t)image->header.w * image->header.h *
                            (opaque ? 2u : 3u) ||
#endif
        !image->data) {
        iw_gui_fault_raise();
        return false;
    }
    lv_area_t source_area = *area;
    if (scaled_app) {
        source_area.x1 += (lv_area_get_width(area) - (int)image->header.w) / 2;
        source_area.y1 += (lv_area_get_height(area) - (int)image->header.h) / 2;
        source_area.x2 = source_area.x1 + image->header.w - 1;
        source_area.y2 = source_area.y1 + image->header.h - 1;
    }
    lv_draw_task_t *task = lv_draw_add_task_checked(layer, &source_area,
                                                     LV_DRAW_TASK_TYPE_IMAGE);
    if (!task) { iw_gui_fault_raise(); return false; }
    lv_draw_image_dsc_t *draw = task->draw_dsc;
    lv_draw_image_dsc_init(draw);
    draw->src = image;
    draw->header = image->header;
    draw->image_area = source_area;
    if (scaled_app || rotation) {
        draw->pivot.x = image->header.w / 2;
        draw->pivot.y = image->header.h / 2;
        draw->scale_x = (int32_t)lv_area_get_width(area) * LV_SCALE_NONE /
                        image->header.w;
        draw->scale_y = draw->scale_x;
        draw->rotation = rotation;
    }
    task->_real_area = *area;
    lv_draw_finalize_task_creation(layer, task);
    return true;
}

static bool cellular_native(const iw_product_view_t *view)
{
    return view->scene.page_id == IW_PAGE_LAUNCHER_GRID;
}

#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
static bool draw_paint(iw_product_view_t *view, lv_layer_t *layer,
                        const lv_area_t *area, unsigned id)
{
    const lv_image_dsc_t *image = iw_v00_paint_image(view->v00_paint_refs, id);
    if (!image) { iw_gui_fault_raise(); return false; }
    /* 与原素材目录分离；程序材质不能放宽 RAW_ALPHA/EZIP 原资源校验。 */
    lv_area_t source = {area->x1, area->y1,
        area->x1 + image->header.w - 1, area->y1 + image->header.h - 1};
    lv_draw_task_t *task = lv_draw_add_task_checked(layer, &source, LV_DRAW_TASK_TYPE_IMAGE);
    if (!task) { iw_gui_fault_raise(); return false; }
    lv_draw_image_dsc_t *draw = task->draw_dsc;
    lv_draw_image_dsc_init(draw);
    draw->src = image;
    draw->header = image->header;
    draw->image_area = source;
    draw->pivot = (lv_point_t){0, 0};
    draw->scale_x = (int32_t)lv_area_get_width(area) * LV_SCALE_NONE / image->header.w;
    draw->scale_y = (int32_t)lv_area_get_height(area) * LV_SCALE_NONE / image->header.h;
    task->_real_area = *area;
    lv_draw_finalize_task_creation(layer, task);
    return true;
}

#endif

/* 与 SDK 示例相同，每个图标独立缩放、移动，避免拖动时重绘整个页面。 */
static bool cellular_refresh(iw_product_view_t *view)
{
    if (!view->v00_images || view->v00_image_count < IW_CELLULAR_ICON_COUNT)
        return false;
    iw_cellular_icon_t geometry[IW_CELLULAR_ICON_COUNT];
    if (!iw_cellular_layout(view->cellular_pan_x, view->cellular_pan_y,
                            view->cellular_zoom, true, geometry)) return false;
    for (unsigned i = 0; i < IW_CELLULAR_ICON_COUNT; ++i) {
        const lv_image_dsc_t *source = view->v00_images[i];
        lv_obj_t *object = view->cellular_icons[i];
        if (!object) {
            object = lv_obj_class_create_obj_checked(&lv_image_class, view->surface);
            if (!object) return false;
            view->cellular_icons[i] = object;
            lv_obj_class_init_obj(object);
            if (!object->spec_attr) {
                lv_obj_spec_attr_t *attr = lv_malloc_zeroed(sizeof(*attr));
                if (!attr) return false;
                attr->scroll_dir = LV_DIR_ALL;
                attr->scrollbar_mode = LV_SCROLLBAR_MODE_AUTO;
                object->spec_attr = attr;
            }
            lv_image_set_src(object, source);
            lv_image_set_pivot(object, source->header.w / 2, source->header.h / 2);
            lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
        iw_cellular_icon_t next = geometry[i];
        if (!next.size) {
            if (!view->cellular_bounds[i].size) continue;
            view->cellular_bounds[i].size = 0;
            lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        if (view->cellular_bounds[i].x == next.x &&
            view->cellular_bounds[i].y == next.y &&
            view->cellular_bounds[i].size == next.size &&
            !lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) continue;
        view->cellular_bounds[i] = next;
        int center_x = next.x + next.size / 2;
        int center_y = next.y + next.size / 2;
        uint32_t scale = (uint32_t)next.size * LV_SCALE_NONE / source->header.w;
        if (!scale) scale = 1u;
        lv_image_set_scale(object, scale);
        int pos_x = center_x - source->header.w / 2;
        int pos_y = center_y - source->header.h / 2;
        lv_obj_set_pos(object, pos_x, pos_y);
        lv_style_value_t value;
        if (lv_obj_get_local_style_prop(object, LV_STYLE_X, &value, 0) !=
                LV_STYLE_RES_FOUND || value.num != pos_x ||
            lv_obj_get_local_style_prop(object, LV_STYLE_Y, &value, 0) !=
                LV_STYLE_RES_FOUND || value.num != pos_y) return false;
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    return true;
}

static int cellular_hit(const iw_product_view_t *view, int x, int y)
{
    for (int i = IW_CELLULAR_ICON_COUNT - 1; i >= 0; --i) {
        const iw_cellular_icon_t *icon = &view->cellular_bounds[i];
        if (!icon->size) continue;
        int dx = x - (icon->x + icon->size / 2);
        int dy = y - (icon->y + icon->size / 2);
        int radius = icon->size / 2;
        if (dx * dx + dy * dy <= radius * radius) return i;
    }
    return -1;
}

static void cellular_move(iw_product_view_t *view, int dx, int dy, int zoom)
{
    int pan_x = view->cellular_pan_x + dx;
    int pan_y = view->cellular_pan_y + dy;
    int next_zoom = view->cellular_zoom + zoom;
    if (pan_x < -96) pan_x = -96;
    if (pan_x > 96) pan_x = 96;
    if (pan_y < -96) pan_y = -96;
    if (pan_y > 96) pan_y = 96;
    if (next_zoom < -30) next_zoom = -30;
    if (next_zoom > 30) next_zoom = 30;
    if (pan_x == view->cellular_pan_x && pan_y == view->cellular_pan_y &&
        next_zoom == view->cellular_zoom) return;
    view->cellular_pan_x = (int16_t)pan_x;
    view->cellular_pan_y = (int16_t)pan_y;
    view->cellular_zoom = (int8_t)next_zoom;
    if (!cellular_refresh(view)) iw_gui_fault_raise();
}

static void destructor(const lv_obj_class_t *class_p, lv_obj_t *object) {
    (void)class_p;
    iw_product_view_t *v = ((product_surface_t *)object)->view;
    if (!v) return;
    v->active = false;
    v->animating = false;
    v->picker_snapping = false;
    v->surface = NULL;
    v->pressed = -1;
    v->face_long_press_fired = false;
    v->press_x = v->last_x = 0;
    v->launcher_zooming = false;
    v->alarm_wheel_field = -1;
    clear_launcher_tap(v);
    memset(v->cellular_icons, 0, sizeof(v->cellular_icons));
    memset(v->cellular_bounds, 0, sizeof(v->cellular_bounds));
    for (unsigned i = 0; i < sizeof(sizes); i++)
        (void)iw_font_release(&v->fonts[i]);
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
    for (unsigned i = 0; i < v->v00_font_count; ++i)
        (void)iw_font_release(&v->v00_fonts[i]);
    v->v00_font_count = 0;
    /* 父屏删除和显式退出共用该路径；忙态保留位集，后续 destroy 重试。 */
    (void)iw_v00_paint_release(&v->v00_paint_refs);
#endif
#ifdef IW_TARGET_BUILD
    (void)release_control_images(v);
#endif
}

static bool make_surface(iw_product_view_t *v, lv_obj_t *parent) {
    lv_obj_style_t *style = lv_malloc_zeroed(sizeof(*style));
    if (!style) return false;
    lv_obj_spec_attr_t *attr = lv_malloc_zeroed(sizeof(*attr));
    if (!attr) {
        lv_free(style);
        return false;
    }
    product_surface_t *s = (product_surface_t *)lv_obj_class_create_obj_checked(&surface_class, parent);
    if (!s) {
        lv_free(attr);
        lv_free(style);
        return false;
    }
    s->view = v;
    s->base.spec_attr = attr;
    attr->scroll_dir = LV_DIR_NONE;
    s->props[0] = (lv_style_const_prop_t){LV_STYLE_X, {.num = 0}};
    s->props[1] = (lv_style_const_prop_t){LV_STYLE_Y, {.num = 0}};
    s->props[2] = (lv_style_const_prop_t){LV_STYLE_WIDTH, {.num = 390}};
    s->props[3] = (lv_style_const_prop_t){LV_STYLE_HEIGHT, {.num = 450}};
    lv_style_init(&s->geometry);
    s->geometry.prop_cnt = 255;
    s->geometry.has_group = UINT32_MAX;
    s->geometry.values_and_props = s->props;
    v->surface = &s->base;
    lv_obj_class_init_obj(&s->base);
    style->style = &s->geometry;
    s->base.styles = style;
    s->base.style_cnt = 1;
#if LV_OBJ_STYLE_CACHE
    s->base.style_main_prop_is_set = UINT32_MAX;
#endif
    lv_obj_refresh_style(&s->base, LV_PART_MAIN, LV_STYLE_PROP_ANY);
    lv_obj_remove_flag(&s->base,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(&s->base, LV_OBJ_FLAG_CLICKABLE);
    return true;
}

bool iw_product_view_update(iw_product_view_t *v, const iw_product_model_t *model) {
    if (!v || !v->surface || !model || !iw_theme_effects(model->quality, model->reduced_motion) ||
        !iw_font_port_is_owner() || !iw_font_port_render_idle() ||
        iw_gui_fault_pending())
        return false;
#ifdef IW_TARGET_BUILD
    /* 根 View 会跨表盘提交复用；先核验完整 ROM 目录，再发布模块 scene。
     * 此步失败不改旧 scene，调用者按既有 GUI 故障路径恢复。 */
    if (v->scene.page_id == IW_PAGE_FACE &&
        model->face_session.active_face_id == IW_FACE_MODULAR_LOCAL &&
        (!v->v00_images || v->v00_image_count != IW_ICON_V00_ASSET_COUNT - 1u) &&
        !prepare_foreground_images(v)) {
        iw_gui_fault_raise();
        return false;
    }
#endif
    v->quality = model->quality;
    v->reduced_motion = model->reduced_motion;
    iw_time_field_t old_field = v->scene.picker_field;
    int32_t old_value = v->scene.picker_value;
    if (!iw_product_scene_build(&v->scene, v->scene.page_id, model)) {
        iw_gui_fault_raise();
        return false;
    }
    for (unsigned n = 0; n < v->scene.count; n++) {
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
        if (is_v00_page(&v->scene)) {
            const iw_product_node_t *node = &v->scene.nodes[n];
            unsigned paint = v->scene.v00_materials[n].paint;
            if (paint && !iw_v00_paint_retain(&v->v00_paint_refs, paint)) {
                iw_gui_fault_raise();
                return false;
            }
            if (node->text[0] && !acquire_v00_font(v,
                v->scene.v00_materials[n].font_weight, node->font_px)) {
                iw_gui_fault_raise();
                return false;
            }
            if (node->text[0] && !font_covers_text(
                    v00_font(v, v->scene.v00_materials[n].font_weight, node->font_px),
                    node->text)) {
                if (iw_font_fault_pending() || iw_gui_fault_pending()) {
                    iw_gui_fault_raise();
                    return false;
                }
                unsigned fallback = fallback_font_index(node->font_px);
                if (!v->fonts[fallback].font &&
                    iw_font_acquire(iw_font_find(sizes[fallback]),
                                    &v->fonts[fallback]) != IW_FONT_OK) {
                    iw_gui_fault_raise();
                    return false;
                }
                if (!font_covers_text(v->fonts[fallback].font, node->text)) {
                    iw_gui_fault_raise();
                    return false;
                }
                v->scene.v00_materials[n].fallback_size_px = sizes[fallback];
            }
            continue;
        }
#endif
        unsigned i = font_index(v->scene.nodes[n].font_px);
        if (i == sizeof(sizes) || v->fonts[i].font) continue;
        if (iw_font_acquire(iw_font_find(sizes[i]), &v->fonts[i]) != IW_FONT_OK) {
            iw_gui_fault_raise();
            return false;
        }
    }
    /* 用生产字体的真实度量重排长文本，避免按字符数估计漏掉宽拉丁字形。 */
    for (unsigned i = 0; i < v->scene.count; i++) {
        iw_product_node_t *n = &v->scene.nodes[i];
        unsigned f = font_index(n->font_px);
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
        if (is_v00_page(&v->scene)) continue;
#endif
        if (!n->multiline || f == sizeof(sizes)) continue;
        lv_point_t measured;
        lv_text_get_size(&measured, n->text, v->fonts[f].font, 0, 0, n->width, LV_TEXT_FLAG_NONE);
        if (iw_gui_fault_pending()) return false;
        int delta = measured.y + 12 - n->height;
        n->height += (int16_t)delta;
        for (unsigned j = i + 1; j < v->scene.count; j++) {
            iw_product_node_t *after = &v->scene.nodes[j];
            if (!after->fixed) { after->y += (int16_t)delta; after->baseline += (int16_t)delta; }
        }
        v->scene.content_height = (uint16_t)((int)v->scene.content_height + delta);
    }
    if (old_field != v->scene.picker_field) {
        if (old_field == IW_EDIT_NONE) { v->picker_parent_scroll = v->scroll_y; v->scroll_y = 0; }
        if (v->scene.picker_field == IW_EDIT_NONE) v->scroll_y = v->picker_parent_scroll;
        v->picker_offset = 0;
        v->picker_snapping = false;
    } else if (old_field != IW_EDIT_NONE && old_value != v->scene.picker_value) {
        int32_t step = v->scene.picker_value - old_value;
        v->picker_offset = step >= -1 && step <= 1 ? (int16_t)(v->picker_offset + step * PICKER_STEP_PX) : 0;
        picker_snap(v);
    }
    int limit = iw_product_scene_scroll_limit(&v->scene);
    if (v->scroll_y > limit) v->scroll_y = (int16_t)limit;
    if (cellular_native(v)) {
        v->cellular_pan_x = model->launcher_pan_x;
        v->cellular_pan_y = model->launcher_pan_y;
        v->cellular_zoom = model->launcher_zoom;
        if (!cellular_refresh(v)) {
            iw_gui_fault_raise();
            return false;
        }
    } else lv_obj_invalidate(v->surface);
#ifdef IW_TARGET_BUILD
    if (v->scene.page_id == IW_PAGE_FACE && model->face_session.active_face_id == IW_FACE_DIGITAL) {
        /* 这里只解除借用的 ROM 描述符；字体仍由 View 统一持有至 destroy。 */
        v->v00_images = NULL;
        v->v00_image_count = 0;
    }
#endif
    return true;
}

bool iw_product_view_create(iw_product_view_t *v, lv_obj_t *parent, uint16_t page_id,
                            const iw_product_model_t *model, iw_product_action_fn action,
                            void (*quiesce)(void *), void *context) {
    if (!v || v->frame.object || v->surface || v->control_cache_owned || !model || !iw_font_port_is_owner()) return false;
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
    if (v->v00_paint_refs) return false;
#endif
    if ((page_id == IW_V00_CONTROL_RUNTIME ||
         (page_id >= IW_V00_GRID && page_id < IW_V00_GRID + IW_V00_COUNT) ||
         (page_id >= IW_V00_GRID_RUNTIME && page_id <= IW_V00_DISPLAY_RUNTIME)) &&
        v->v00_image_count < (page_id == IW_V00_GRID ||
                             page_id == IW_V00_GRID_RUNTIME ? IW_ICON_V00_APP_COUNT :
                             IW_ICON_V00_ASSET_COUNT)) return false;
#ifdef IW_TARGET_BUILD
    bool foreground_page = page_id == IW_PAGE_LAUNCHER_GRID ||
        page_id == IW_PAGE_TIMER_LIST || page_id == IW_PAGE_ALARM_EDIT ||
        page_id == IW_PAGE_DISPLAY ||
        (page_id == IW_PAGE_FACE &&
         model->face_session.active_face_id == IW_FACE_MODULAR_LOCAL);
    if (page_id == IW_PAGE_CONTROL_CENTER) {
        if (!prepare_control_images(v)) return false;
    } else if (foreground_page && !prepare_foreground_images(v)) {
        return false;
    }
#endif
    v->scene.page_id = page_id;
    v->scene.picker_field = IW_EDIT_NONE;
    v->pressed = -1;
    v->press_x = v->last_x = 0;
    v->feedback_node = -1;
    v->press_value = 0;
    v->animating = false;
    v->picker_snapping = false;
    v->picker_offset = 0;
    v->face_long_press_fired = false;
    clear_launcher_tap(v);
    v->action = action;
    v->context = context;
    if (iw_screen_frame_create(&v->frame, parent, IW_FRAME_FULLSCREEN, "", quiesce, context) !=
        IW_COMPONENT_OK) {
#ifdef IW_TARGET_BUILD
        if (page_id == IW_PAGE_CONTROL_CENTER) (void)release_control_images(v);
        if (foreground_page) {
            v->v00_images = NULL;
            v->v00_image_count = 0;
        }
#endif
        return false;
    }
    if (!make_surface(v, iw_screen_frame_content(&v->frame)) || !iw_product_view_update(v, model)) {
        (void)iw_product_view_destroy(v);
        return false;
    }
    v->active = true;
    v->resumed_ms = lv_tick_get();
    v->first_draw = true;
    return true;
}

bool iw_product_view_set_v00_images(iw_product_view_t *v,
    const lv_image_dsc_t *const *images, unsigned count)
{
    if (!v || v->surface || v->frame.object || v->control_cache_owned || !iw_font_port_is_owner()) return false;
    if (!images && count == 0u) {
        v->v00_images = NULL;
        v->v00_image_count = 0;
        return true;
    }
    if (!images || (count != IW_ICON_V00_APP_COUNT &&
                    count != IW_ICON_V00_ASSET_COUNT)) return false;
    for (unsigned i = 0; i < count; i++)
        if (!images[i] || images[i]->header.magic != LV_IMAGE_HEADER_MAGIC ||
            images[i]->header.cf != (uint8_t)(i == IW_ICON_V00_ASSET_COUNT - 1u &&
                                              count == IW_ICON_V00_ASSET_COUNT ?
                                              LV_COLOR_FORMAT_RGB565 :
                                              LV_COLOR_FORMAT_RGB565A8) ||
            !images[i]->data)
            return false;
    v->v00_images = images;
    v->v00_image_count = (uint8_t)count;
    return true;
}

bool iw_product_view_destroy(iw_product_view_t *v) {
    if (!v || iw_component_destroy(&v->frame) != IW_COMPONENT_OK) return false;
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
    if (!iw_v00_paint_release(&v->v00_paint_refs)) return false;
#endif
#ifdef IW_TARGET_BUILD
    if (!release_control_images(v)) return false;
    v->v00_images = NULL;
    v->v00_image_count = 0;
#endif
    return true;
}

void iw_product_view_activate(iw_product_view_t *v, bool active) {
    if (!v || !iw_font_port_is_owner()) return;
    v->active = active;
    v->resumed_ms = lv_tick_get();
    v->first_draw = active;
    v->pressed = -1;
    v->dragging = false;
    v->press_value = 0;
    v->animating = false;
    v->picker_snapping = false;
    v->picker_offset = 0;
    v->launcher_zooming = false;
    v->zoom_drag_remainder = 0;
    v->alarm_wheel_field = -1;
    v->alarm_wheel_remainder = 0;
    clear_launcher_tap(v);
    if (v->surface) lv_obj_invalidate(v->surface);
}

void iw_product_view_scroll(iw_product_view_t *v, int32_t delta) {
    if (!v || !v->surface || !iw_font_port_is_owner()) return;
    int64_t next = (int64_t)v->scroll_y + delta;
    int limit = iw_product_scene_scroll_limit(&v->scene);
    v->scroll_y = (int16_t)(next < 0 ? 0 : next > limit ? limit : next);
    lv_obj_invalidate(v->surface);
}

static void draw(iw_product_view_t *v, lv_layer_t *layer) {
    if (iw_gui_fault_pending()) return;
    if (cellular_native(v)) return;
    lv_area_t original = layer->_clip_area;
    lv_area_t origin = v->surface->coords;
    const iw_theme_effects_t *effects = iw_theme_effects(v->quality, v->reduced_motion);
    for (unsigned i = 0; i < v->scene.count && !iw_gui_fault_pending(); i++) {
        const iw_product_node_t *n = &v->scene.nodes[i];
        int shift = n->fixed ? 0 : v->scroll_y;
        if (n->picker_item) shift -= v->picker_offset;
        int clip_bottom = n->fixed ? 450 : v->scene.clip_bottom;
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
        if (!n->fixed && is_v00_page(&v->scene) && v->scene.v00_materials[i].clip_bottom)
            clip_bottom = v->scene.v00_materials[i].clip_bottom;
#endif
        lv_area_t clip = {origin.x1, origin.y1 + (n->fixed ? 0 : v->scene.clip_top), origin.x2,
                          origin.y1 + clip_bottom - 1};
        if (!lv_area_intersect(&layer->_clip_area, &original, &clip)) continue;
        if (n->picker_item) {
            lv_area_t picker_clip = {origin.x1, origin.y1 + PICKER_TOP, origin.x2,
                                    origin.y1 + PICKER_BOTTOM - 1};
            if (!lv_area_intersect(&layer->_clip_area, &layer->_clip_area, &picker_clip)) continue;
        }
        lv_area_t area = {origin.x1 + n->x, origin.y1 + n->y - shift, origin.x1 + n->x + n->width - 1,
                          origin.y1 + n->y - shift + n->height - 1};
        const iw_product_node_t *hit = v->feedback_node >= 0 && v->feedback_node < (int)v->scene.count
            ? &v->scene.nodes[v->feedback_node] : NULL;
        bool feedback_active = v->press_value && hit && n->fixed == hit->fixed &&
            n->x >= hit->x && n->y >= hit->y && n->x + n->width <= hit->x + hit->width &&
            n->y + n->height <= hit->y + hit->height;
        /* 只变换绘制区域；命中区始终保持原来的尺寸。 */
        if (feedback_active && !v->reduced_motion && hit->width == hit->height &&
            !(is_v00_page(&v->scene) && n->icon >= IW_ICON_V00_APP_FIRST)) {
            int shrink = (1000 - effects->pressed_scale_permille) * v->press_value / 1000;
            int cx = origin.x1 + hit->x + hit->width / 2;
            int cy = origin.y1 + hit->y - shift + hit->height / 2;
            area.x1 -= (area.x1 - cx) * shrink / 1000;
            area.x2 -= (area.x2 - cx) * shrink / 1000;
            area.y1 -= (area.y1 - cy) * shrink / 1000;
            area.y2 -= (area.y2 - cy) * shrink / 1000;
        }
        uint32_t fill = n->fill;
        if (feedback_active && n == hit) fill = IW_PRODUCT_PRESSED;
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
        const iw_v00_material_t *material = &v->scene.v00_materials[i];
        if (material->paint) {
            if (!draw_paint(v, layer, &area, material->paint)) break;
        } else if (fill && material->gradient) {
            /* 固定 SDK 的软件绘制只开启轴向渐变，主机样片保留端点与透明度。 */
            if (!iw_draw_fill_checked(layer, &area, fill, n->radius,
                                      material->opacity, true, material->end_color)) break;
        } else
#endif
        if (fill && !iw_draw_fill_checked(layer, &area, fill, n->radius, 255, false, 0)) break;
        if (fill && n->fixed && n->width == 48 && n->height == 48 && v->quality == IW_THEME_Q1 &&
            !iw_draw_fill_checked(layer, &area, IW_PRODUCT_WHITE, n->radius, 24, true, fill)) break;
        if (is_v00_page(&v->scene) &&
            n->icon >= IW_ICON_V00_APP_FIRST &&
            n->icon < IW_ICON_V00_APP_FIRST + IW_ICON_V00_ASSET_COUNT) {
            if (!draw_v00_image(v, layer, &area, n->icon - IW_ICON_V00_APP_FIRST,
                                material->image_rotation)) break;
        } else if (n->icon) draw_icon(layer, &area, n->icon, n->color);
        if (!n->text[0]) continue;
        const lv_font_t *font = NULL;
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
        if (is_v00_page(&v->scene))
            font = material->fallback_size_px ?
                v->fonts[font_index(material->fallback_size_px)].font :
                v00_font(v, material->font_weight, n->font_px);
        else
#endif
        {
            unsigned f = font_index(n->font_px);
            if (f < sizeof(sizes)) font = v->fonts[f].font;
        }
        if (!font) {
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
            if (is_v00_page(&v->scene)) iw_gui_fault_raise();
#endif
            break;
        }
        /* 基线来自字体度量，不能把对象顶部当成文字基线。 */
        area.y1 = origin.y1 + n->baseline - shift - font->line_height + font->base_line;
        if (!n->multiline) area.y2 = area.y1 + font->line_height - 1;
        static const lv_text_align_t alignment[] = {LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_CENTER,
                                                    LV_TEXT_ALIGN_RIGHT};
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
        if (is_v00_page(&v->scene)) {
            const iw_v00_material_t *type = &v->scene.v00_materials[i];
            if (!iw_draw_text_checked_spaced(layer, &area, n->text, font, n->color,
                                             alignment[n->align], 255, type->letter_space))
                break;
            continue;
        }
#endif
        (void)iw_draw_text_checked(layer, &area, n->text, font, n->color, alignment[n->align], 255);
    }
    layer->_clip_area = original;
}

static void event(const lv_obj_class_t *class_p, lv_event_t *e) {
    (void)class_p;
    product_surface_t *surface = (product_surface_t *)lv_event_get_current_target(e);
    iw_product_view_t *v = surface->view;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DRAW_MAIN) {
        iw_render_probe_draw(v->scene.page_id, lv_tick_get() - v->resumed_ms, v->first_draw);
        v->first_draw = false;
        draw(v, lv_event_get_layer(e));
        return;
    }
    if (code == LV_EVENT_DRAW_POST) return;
    if (code == LV_EVENT_COVER_CHECK) {
        ((lv_cover_check_info_t *)lv_event_get_param(e))->res = LV_COVER_RES_NOT_COVER;
        return;
    }
    if (lv_obj_event_base(&surface_class, e) != LV_RESULT_OK) return;
    if (!v || !v->active || iw_gui_fault_pending()) return;
    if (code == LV_EVENT_INDEV_RESET || code == LV_EVENT_PRESS_LOST) {
        feedback(v, false);
        v->pressed = -1;
        v->dragging = false;
        v->face_long_press_fired = false;
        v->picker_offset = 0;
        v->picker_snapping = false;
        v->launcher_zooming = false;
        v->alarm_wheel_field = -1;
        v->alarm_wheel_remainder = 0;
        clear_launcher_tap(v);
        lv_obj_invalidate(&surface->base);
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
    /* 拖动先更新模型，不能先绘制一帧旧坐标再等待新场景提交。 */
    bool invalidate = code != LV_EVENT_PRESSING || v->scene.page_id != IW_PAGE_LAUNCHER_GRID;
    lv_indev_t *input = lv_event_get_indev(e);
    if (!input) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    int x = point.x - surface->base.coords.x1, y = point.y - surface->base.coords.y1;
    if (code == LV_EVENT_PRESSED) {
        v->pressed = (int16_t)(cellular_native(v) ? cellular_hit(v, x, y) :
                               iw_product_scene_hit(&v->scene, x, y, v->scroll_y));
        if (v->pressed >= 0) clear_launcher_tap(v);
        v->launcher_zooming = v->scene.page_id == IW_PAGE_LAUNCHER_GRID &&
            v->pressed < 0 && v->last_tap_valid &&
            (uint32_t)(lv_tick_get() - v->last_tap_ms) <= 350u &&
            x - v->last_tap_x <= 40 && v->last_tap_x - x <= 40 &&
            y - v->last_tap_y <= 40 && v->last_tap_y - y <= 40;
        v->zoom_drag_remainder = 0;
        v->press_x = v->last_x = (int16_t)x;
        v->last_y = v->press_y = (int16_t)y;
        v->dragging = false;
        v->picker_snapping = false;
        v->picker_offset = 0;
        feedback(v, v->pressed >= 0 && !cellular_native(v));
        v->face_long_press_fired = false;
    }
    uint16_t action = v->pressed >= 0 ?
        (cellular_native(v) ? v->cellular_bounds[v->pressed].action :
                              v->scene.nodes[v->pressed].action) : 0;
    if (code == LV_EVENT_PRESSED) {
        v->alarm_wheel_field = action == IW_ACTION_ALARM_HOUR_PLUS ||
                               action == IW_ACTION_ALARM_HOUR_MINUS ? 0 :
                               action == IW_ACTION_ALARM_MINUTE_PLUS ||
                               action == IW_ACTION_ALARM_MINUTE_MINUS ? 1 : -1;
        v->alarm_wheel_remainder = 0;
    }
    if (code == LV_EVENT_PRESSING && v->scene.page_id == IW_PAGE_FACE &&
        !v->dragging && !v->face_long_press_fired &&
        (uint32_t)(lv_tick_get() - v->press_started) >= 700u) {
        v->face_long_press_fired = true;
        feedback(v, false);
        if (v->action) v->action(IW_ACTION_FACE_PICKER, 0, true, v->context);
    } else if (action == IW_ACTION_TRACK) {
        if (v->action) {
            uint8_t level = v->scene.page_id == IW_PAGE_CONTROL_CENTER
                ? iw_brightness_track_level(x)
                : v->scene.page_id == IW_PAGE_DISPLAY && v->scene.v00_style
                    ? iw_brightness_v00_display_level(x)
                    : iw_brightness_detail_level(x);
            v->action(action, level, code == LV_EVENT_RELEASED, v->context);
        }
    } else if (code == LV_EVENT_PRESSING) {
        bool horizontal_switcher = v->scene.page_id == IW_PAGE_SWITCHER;
        bool launcher_grid = v->scene.page_id == IW_PAGE_LAUNCHER_GRID;
        if (!v->dragging && (v->alarm_wheel_field >= 0 ?
                             (y - v->press_y > 5 || v->press_y - y > 5) :
                             launcher_grid ?
                             (x - v->press_x > 8 || v->press_x - x > 8 ||
                              y - v->press_y > 8 || v->press_y - y > 8) :
                             (horizontal_switcher ?
                              (x - v->press_x > 12 || v->press_x - x > 12) :
                              (y - v->press_y > 12 || v->press_y - y > 12)))) {
            v->dragging = true;
            if (launcher_grid) clear_launcher_tap(v);
            if (launcher_grid) {
                v->animating = false;
                v->press_value = v->press_to = 0;
                v->feedback_node = -1;
            } else feedback(v, false);
        }
        if (v->dragging && (horizontal_switcher || launcher_grid)) {
            if (launcher_grid && v->action) {
                if (v->launcher_zooming) {
                    int pixels = v->zoom_drag_remainder + v->last_y - y;
                    int steps = pixels / 2;
                    v->zoom_drag_remainder = (int8_t)(pixels % 2);
                    if (steps) {
                        cellular_move(v, 0, 0, steps);
                        v->action(IW_ACTION_LAUNCHER_ZOOM_DRAG,
                                  steps, false, v->context);
                    }
                } else {
                    int dx = x - v->last_x, dy = y - v->last_y;
                    if (dx || dy) {
                        uint32_t packed = ((uint32_t)(uint16_t)(int16_t)dx << 16) |
                                          (uint16_t)(int16_t)dy;
                        cellular_move(v, dx, dy, 0);
                        v->action(IW_ACTION_LAUNCHER_PAN, (int32_t)packed,
                                  false, v->context);
                    }
                }
            }
            v->last_x = (int16_t)x;
            v->last_y = (int16_t)y;
        } else if (v->dragging && v->alarm_wheel_field >= 0) {
            int pixels = v->alarm_wheel_remainder + v->last_y - y;
            int steps = pixels / 14;
            v->alarm_wheel_remainder = (int16_t)(pixels % 14);
            if (steps && v->action) {
                uint32_t packed = ((uint32_t)(unsigned)v->alarm_wheel_field << 16) |
                                  (uint16_t)(int16_t)steps;
                v->action(IW_ACTION_ALARM_WHEEL, (int32_t)packed,
                          false, v->context);
            }
        } else if (v->dragging && v->scene.picker_field != IW_EDIT_NONE &&
            v->press_y >= PICKER_TOP && v->press_y < PICKER_BOTTOM) {
            int offset = y - v->press_y;
            v->picker_offset = (int16_t)(offset < -PICKER_STEP_PX ? -PICKER_STEP_PX :
                                        offset > PICKER_STEP_PX ? PICKER_STEP_PX : offset);
        } else if (v->dragging && v->press_y >= v->scene.clip_top && v->press_y < v->scene.clip_bottom)
            iw_product_view_scroll(v, v->last_y - y);
        v->last_y = (int16_t)y;
    } else if (code == LV_EVENT_RELEASED) {
        bool launcher_grid = v->scene.page_id == IW_PAGE_LAUNCHER_GRID;
        if (launcher_grid && !v->dragging &&
            (x - v->press_x > 8 || v->press_x - x > 8 ||
             y - v->press_y > 8 || v->press_y - y > 8)) {
            v->dragging = true;
            feedback(v, false);
        }
        if (launcher_grid && v->dragging) invalidate = false;
        int pressed = v->pressed;
        bool click = !v->dragging && !v->face_long_press_fired && pressed >= 0 &&
                     (cellular_native(v) ? cellular_hit(v, x, y) :
                      iw_product_scene_hit(&v->scene, x, y, v->scroll_y)) == pressed;
        v->pressed = -1;
        if (v->dragging && launcher_grid && v->launcher_zooming) {
            clear_launcher_tap(v);
        } else if (v->dragging && launcher_grid && v->action) {
            int dx = x - v->last_x, dy = y - v->last_y;
            if (dx || dy) {
                uint32_t packed = ((uint32_t)(uint16_t)(int16_t)dx << 16) |
                                  (uint16_t)(int16_t)dy;
                cellular_move(v, dx, dy, 0);
                v->action(IW_ACTION_LAUNCHER_PAN, (int32_t)packed, true, v->context);
            }
            clear_launcher_tap(v);
        } else if (v->dragging && v->alarm_wheel_field >= 0) {
            /* 拖动已经逐格更新草稿，松手不再触发点按或页面滚动。 */
        } else if (v->dragging && v->scene.page_id == IW_PAGE_SWITCHER) {
            int delta = x - v->press_x;
            if (delta <= -48 && v->action)
                v->action(IW_ACTION_RECENT_NEXT, 1, true, v->context);
            else if (delta >= 48 && v->action)
                v->action(IW_ACTION_RECENT_PREVIOUS, -1, true, v->context);
        } else if (v->dragging && v->scene.picker_field != IW_EDIT_NONE &&
            v->press_y >= PICKER_TOP && v->press_y < PICKER_BOTTOM) {
            int steps = v->picker_offset > PICKER_STEP_PX / 2 ? -1 :
                        v->picker_offset < -PICKER_STEP_PX / 2 ? 1 : 0;
            if (steps && v->action) v->action(IW_ACTION_PICK_STEP, steps, true, v->context);
            picker_snap(v);
        } else if (v->dragging && v->scene.page_id == IW_PAGE_FACE && v->action) {
            if (v->press_y < 104 && y - v->press_y >= 64)
                v->action(IW_ACTION_FACE_NOTIFICATIONS, 0, true, v->context);
            else if (v->press_y >= 320 && v->press_y - y >= 64)
                v->action(IW_ACTION_FACE_STACK, 0, true, v->context);
        } else if (click && v->action) {
            clear_launcher_tap(v);
            v->action(action, 0, true, v->context);
        } else if (launcher_grid && !v->dragging && pressed < 0) {
            uint32_t now = lv_tick_get();
            bool double_tap = v->last_tap_valid && (uint32_t)(now - v->last_tap_ms) <= 350u &&
                              (x - v->last_tap_x <= 40 && v->last_tap_x - x <= 40) &&
                              (y - v->last_tap_y <= 40 && v->last_tap_y - y <= 40);
            if (double_tap && v->action) {
                cellular_move(v, 0, 0, v->cellular_zoom >= 30 ? -60 : 15);
                v->action(IW_ACTION_LAUNCHER_ZOOM, 1, true, v->context);
                clear_launcher_tap(v);
            } else {
                v->last_tap_valid = true;
                v->last_tap_ms = now;
                v->last_tap_x = (int16_t)x;
                v->last_tap_y = (int16_t)y;
            }
        }
    }
    if (code == LV_EVENT_RELEASED) {
        feedback(v, false);
        v->pressed = -1;
        v->dragging = false;
        v->face_long_press_fired = false;
        v->launcher_zooming = false;
        v->alarm_wheel_field = -1;
    }
    if (invalidate) lv_obj_invalidate(&surface->base);
}
