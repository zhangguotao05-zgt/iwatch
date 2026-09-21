#include "iw_product_view.h"
#include "iw_draw_checked.h"
#include "iw_gui_owner.h"
#include "iw_font_port.h"
#include "iw_render_probe.h"
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_event_private.h"
#include "src/core/lv_obj_class_private.h"
#include "src/core/lv_obj_style_private.h"
#include "src/draw/lv_draw_private.h"
#include "src/misc/lv_area_private.h"
#include <string.h>

typedef struct {
    lv_obj_t base;
    iw_product_view_t *view;
    lv_style_t geometry;
    lv_style_const_prop_t props[5];
} product_surface_t;
static const uint8_t sizes[] = {20, 22, 26, 30, 80};
#define PICKER_STEP_PX 75
#define PICKER_TOP 116
#define PICKER_BOTTOM 326
static void event(const lv_obj_class_t *class_p, lv_event_t *e);
static void destructor(const lv_obj_class_t *class_p, lv_obj_t *object);
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

static void feedback(iw_product_view_t *v, bool down) {
    if (down) v->feedback_node = v->pressed;
    v->press_from = v->press_value;
    v->press_to = down ? 1000 : 0;
    v->press_started = lv_tick_get();
    const iw_theme_effects_t *effects = iw_theme_effects(v->quality, v->reduced_motion);
    v->animating = effects->press_ms && v->press_from != v->press_to;
    if (!v->animating) v->press_value = v->press_to;
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
    if (changed) lv_obj_invalidate(v->surface);
    return v->animating || v->picker_snapping ? 16u : UINT32_MAX;
}

static void icon_line(lv_layer_t *layer, const lv_area_t *a, uint32_t color, int x1, int y1, int x2, int y2) {
    int size = lv_area_get_width(a), width = size >= 40 ? 3 : 2;
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
    if (icon == IW_ICON_BACK || icon == IW_ICON_NEXT) {
        int start = icon == IW_ICON_BACK ? 29 : 17, middle = icon == IW_ICON_BACK ? 17 : 31;
        icon_line(layer, a, color, start, 12, middle, 24);
        icon_line(layer, a, color, middle, 24, start, 36);
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
    for (unsigned i = 0; i < sizeof(sizes); i++)
        (void)iw_font_release(&v->fonts[i]);
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
    v->quality = model->quality;
    v->reduced_motion = model->reduced_motion;
    iw_time_field_t old_field = v->scene.picker_field;
    int32_t old_value = v->scene.picker_value;
    if (!iw_product_scene_build(&v->scene, v->scene.page_id, model)) {
        iw_gui_fault_raise();
        return false;
    }
    for (unsigned n = 0; n < v->scene.count; n++) {
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
    lv_obj_invalidate(v->surface);
    return true;
}

bool iw_product_view_create(iw_product_view_t *v, lv_obj_t *parent, uint16_t page_id,
                            const iw_product_model_t *model, iw_product_action_fn action,
                            void (*quiesce)(void *), void *context) {
    if (!v || v->frame.object || v->surface || !model || !iw_font_port_is_owner()) return false;
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
    v->action = action;
    v->context = context;
    if (iw_screen_frame_create(&v->frame, parent, IW_FRAME_FULLSCREEN, "", quiesce, context) !=
        IW_COMPONENT_OK)
        return false;
    if (!make_surface(v, iw_screen_frame_content(&v->frame)) || !iw_product_view_update(v, model)) {
        (void)iw_product_view_destroy(v);
        return false;
    }
    v->active = true;
    v->resumed_ms = lv_tick_get();
    v->first_draw = true;
    return true;
}

bool iw_product_view_destroy(iw_product_view_t *v) {
    return v && iw_component_destroy(&v->frame) == IW_COMPONENT_OK;
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
    lv_area_t original = layer->_clip_area;
    lv_area_t origin = v->surface->coords;
    const iw_theme_effects_t *effects = iw_theme_effects(v->quality, v->reduced_motion);
    for (unsigned i = 0; i < v->scene.count && !iw_gui_fault_pending(); i++) {
        const iw_product_node_t *n = &v->scene.nodes[i];
        int shift = n->fixed ? 0 : v->scroll_y;
        if (n->picker_item) shift -= v->picker_offset;
        lv_area_t clip = {origin.x1, origin.y1 + (n->fixed ? 0 : v->scene.clip_top), origin.x2,
                          origin.y1 + (n->fixed ? 450 : v->scene.clip_bottom) - 1};
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
        if (feedback_active && !v->reduced_motion && hit->width == hit->height) {
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
        if (fill && !iw_draw_fill_checked(layer, &area, fill, n->radius, 255, false, 0)) break;
        if (fill && n->fixed && n->width == 48 && n->height == 48 && v->quality == IW_THEME_Q1 &&
            !iw_draw_fill_checked(layer, &area, IW_PRODUCT_WHITE, n->radius, 24, true, fill)) break;
        if (n->icon) draw_icon(layer, &area, n->icon, n->color);
        unsigned f = font_index(n->font_px);
        if (!n->text[0] || f == sizeof(sizes) || !v->fonts[f].font) continue;
        const lv_font_t *font = v->fonts[f].font;
        /* 基线来自字体度量，不能把对象顶部当成文字基线。 */
        area.y1 = origin.y1 + n->baseline - shift - font->line_height + font->base_line;
        if (!n->multiline) area.y2 = area.y1 + font->line_height - 1;
        static const lv_text_align_t alignment[] = {LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_CENTER,
                                                    LV_TEXT_ALIGN_RIGHT};
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
        lv_obj_invalidate(&surface->base);
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
    lv_indev_t *input = lv_event_get_indev(e);
    if (!input) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    int x = point.x - surface->base.coords.x1, y = point.y - surface->base.coords.y1;
    if (code == LV_EVENT_PRESSED) {
        v->pressed = (int16_t)iw_product_scene_hit(&v->scene, x, y, v->scroll_y);
        v->press_x = v->last_x = (int16_t)x;
        v->last_y = v->press_y = (int16_t)y;
        v->dragging = false;
        v->picker_snapping = false;
        v->picker_offset = 0;
        feedback(v, v->pressed >= 0);
        v->face_long_press_fired = false;
    }
    uint16_t action = v->pressed >= 0 ? v->scene.nodes[v->pressed].action : 0;
    if (code == LV_EVENT_PRESSING && v->scene.page_id == IW_PAGE_FACE &&
        !v->dragging && !v->face_long_press_fired &&
        (uint32_t)(lv_tick_get() - v->press_started) >= 700u) {
        v->face_long_press_fired = true;
        feedback(v, false);
        if (v->action) v->action(IW_ACTION_FACE_PICKER, 0, true, v->context);
    } else if (action == IW_ACTION_TRACK) {
        if (v->action) {
            uint8_t level = v->scene.page_id == IW_PAGE_CONTROL_CENTER
                ? iw_brightness_track_level(x) : iw_brightness_detail_level(x);
            v->action(action, level, code == LV_EVENT_RELEASED, v->context);
        }
    } else if (code == LV_EVENT_PRESSING) {
        bool horizontal_switcher = v->scene.page_id == IW_PAGE_SWITCHER;
        if (!v->dragging && (horizontal_switcher ?
                             (x - v->press_x > 12 || v->press_x - x > 12) :
                             (y - v->press_y > 12 || v->press_y - y > 12))) {
            v->dragging = true;
            feedback(v, false);
        }
        if (v->dragging && horizontal_switcher) {
            v->last_x = (int16_t)x;
        } else if (v->dragging && v->scene.picker_field != IW_EDIT_NONE &&
            v->press_y >= PICKER_TOP && v->press_y < PICKER_BOTTOM) {
            int offset = y - v->press_y;
            v->picker_offset = (int16_t)(offset < -PICKER_STEP_PX ? -PICKER_STEP_PX :
                                        offset > PICKER_STEP_PX ? PICKER_STEP_PX : offset);
        } else if (v->dragging && v->press_y >= v->scene.clip_top && v->press_y < v->scene.clip_bottom)
            iw_product_view_scroll(v, v->last_y - y);
        v->last_y = (int16_t)y;
    } else if (code == LV_EVENT_RELEASED) {
        bool click = !v->dragging && !v->face_long_press_fired && v->pressed >= 0 &&
                     iw_product_scene_hit(&v->scene, x, y, v->scroll_y) == v->pressed;
        v->pressed = -1;
        if (v->dragging && v->scene.page_id == IW_PAGE_SWITCHER) {
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
        } else if (click && v->action) v->action(action, 0, true, v->context);
    }
    if (code == LV_EVENT_RELEASED) {
        feedback(v, false);
        v->pressed = -1;
        v->dragging = false;
        v->face_long_press_fired = false;
    }
    lv_obj_invalidate(&surface->base);
}
