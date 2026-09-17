#include "iw_components.h"
#include "iw_font.h"
#include "iw_font_port.h"
#include "iw_gui_owner.h"
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_event_private.h"
#include "src/core/lv_obj_class_private.h"
#include "src/core/lv_obj_style_private.h"
#include "src/draw/lv_draw_private.h"
#include "src/misc/lv_area_private.h"
#include <string.h>

enum { TITLE_BYTES = 128, DETAIL_BYTES = 256, VALUE_BYTES = 48, GEOMETRY_PROPS = 5 };

typedef struct component {
    lv_obj_t base;
    struct component *next;
    iw_component_t *handle, content;
    iw_component_kind_t kind;
    iw_component_config_t config;
    iw_component_state_t state;
    iw_font_ref_t primary, secondary;
    iw_gui_owner_t owner;
    void (*quiesce)(void *);
    void *page_context;
    char title[TITLE_BYTES], detail[DETAIL_BYTES], value[VALUE_BYTES];
    /* 固定属性表由此对象独占；不调用会扩容的通用样式修改接口。 */
    lv_style_t geometry;
    lv_style_const_prop_t props[GEOMETRY_PROPS];
    uint32_t press_started;
    uint16_t press_from, press_to, press_value;
    bool pressed, animating;
} component_t;

static component_t *components;
static void component_event(const lv_obj_class_t *class_p, lv_event_t *event);
static void component_destructor(const lv_obj_class_t *class_p, lv_obj_t *object);
static const lv_obj_class_t component_class = {
    .base_class = &lv_obj_class, .event_cb = component_event,
    .destructor_cb = component_destructor, .instance_size = sizeof(component_t),
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE, .name = "iw_component"
};

static component_t *get(const iw_component_t *handle)
{
    return handle && lv_obj_check_type(handle->object, &component_class) ? (component_t *)handle->object : NULL;
}

/* 有界 UTF-8 检查拒绝截断、多余续字节、代理区和过长编码；空指针代表空文案。 */
static bool text_valid(const char *text, size_t capacity)
{
    if (!text) return true;
    size_t i = 0;
    while (i < capacity && text[i]) {
        uint8_t first = (uint8_t)text[i++];
        uint32_t code; unsigned count;
        if (first < 0x80) continue;
        if (first >= 0xc2 && first <= 0xdf) { code = first & 0x1fu; count = 1; }
        else if (first >= 0xe0 && first <= 0xef) { code = first & 0x0fu; count = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { code = first & 7u; count = 3; }
        else return false;
        unsigned tail_count = count;
        while (count--) {
            if (i >= capacity || ((uint8_t)text[i] & 0xc0u) != 0x80u) return false;
            code = (code << 6) | ((uint8_t)text[i++] & 0x3fu);
        }
        if ((tail_count == 2 && code < 0x800) || (tail_count == 3 && code < 0x10000) ||
            (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff) return false;
    }
    return i < capacity;
}

static bool view_valid(iw_component_kind_t kind, const iw_component_view_t *view)
{
    if (!view || !text_valid(view->title, TITLE_BYTES) || !text_valid(view->detail, DETAIL_BYTES) ||
        !text_valid(view->value, VALUE_BYTES)) return false;
    if (kind != IW_LIST_ROW && view->value && view->value[0]) return false;
    if ((kind == IW_SCREEN_FRAME || kind == IW_PILL_BUTTON) && view->detail && view->detail[0]) return false;
    switch (kind) {
    case IW_SCREEN_FRAME: return view->state == IW_NORMAL;
    case IW_LIST_ROW: return view->state == IW_NORMAL || view->state == IW_PRESSED || view->state == IW_DISABLED;
    case IW_PILL_BUTTON: return view->state >= IW_NORMAL && view->state <= IW_DANGER;
    case IW_STATE_PANEL: return view->state >= IW_LOADING && view->state <= IW_UNAVAILABLE;
    case IW_STATUS_BANNER: return view->state == IW_INFO || view->state == IW_SUCCESS ||
                                 view->state == IW_WARNING || view->state == IW_DANGER;
    default: return false;
    }
}

static void copy_text(char *target, const char *source)
{
    if (source) memmove(target, source, strlen(source) + 1);
    else target[0] = 0;
}

static void copy_view(component_t *component, const iw_component_view_t *view)
{
    component->state = view->state;
    copy_text(component->title, view->title);
    copy_text(component->detail, view->detail);
    copy_text(component->value, view->value);
}

static void press_to(component_t *component, bool down)
{
    const iw_theme_effects_t *effects = iw_theme_effects(component->config.quality, component->config.reduced_motion);
    component->pressed = down;
    component->press_from = component->press_value;
    component->press_to = down ? 1000 : 0;
    component->press_started = lv_tick_get();
    component->animating = effects->press_ms && component->press_from != component->press_to;
    if (!component->animating) component->press_value = component->press_to;
    lv_obj_invalidate(&component->base);
}

static bool interactive(const component_t *component)
{
    return (component->kind == IW_LIST_ROW || component->kind == IW_PILL_BUTTON) &&
        component->state != IW_DISABLED && component->state != IW_WAITING;
}

static void stop_frame(void *context)
{
    (void)iw_component_destroy(context);
}

static component_t *create_object(iw_component_t *handle, lv_obj_t *parent,
    iw_component_kind_t kind, const iw_component_config_t *config)
{
    /* 所有可失败的附属分配都在提交父子关系前完成。 */
    lv_obj_style_t *styles = lv_malloc_zeroed(sizeof(*styles));
    if (!styles) return NULL;
    lv_obj_spec_attr_t *attr = lv_malloc_zeroed(sizeof(*attr));
    if (!attr) { lv_free(styles); return NULL; }
    component_t *component = (component_t *)lv_obj_class_create_obj_checked(&component_class, parent);
    if (!component) { lv_free(attr); lv_free(styles); return NULL; }
    component->kind = kind;
    component->config = *config;
    component->handle = handle;
    component->base.spec_attr = attr;
    attr->scroll_dir = LV_DIR_VER;
    attr->scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    component->props[0] = (lv_style_const_prop_t){LV_STYLE_X, {.num = config->x}};
    component->props[1] = (lv_style_const_prop_t){LV_STYLE_Y, {.num = config->y}};
    component->props[2] = (lv_style_const_prop_t){LV_STYLE_WIDTH, {.num = config->width}};
    component->props[3] = (lv_style_const_prop_t){LV_STYLE_HEIGHT, {.num = config->height}};
    lv_style_init(&component->geometry);
    component->geometry.prop_cnt = 255;
    component->geometry.has_group = UINT32_MAX;
    component->geometry.values_and_props = component->props;
    component->next = components;
    components = component;
    handle->object = &component->base;
    lv_obj_class_init_obj(&component->base);
    /* 初始化会清除主题样式；之后再挂固定几何，避免被 lv_theme_apply 清掉。 */
    styles->style = &component->geometry;
    component->base.styles = styles;
    component->base.style_cnt = 1;
#if LV_OBJ_STYLE_CACHE
    component->base.style_main_prop_is_set = UINT32_MAX;
#endif
    lv_obj_refresh_style(&component->base, LV_PART_MAIN, LV_STYLE_PROP_ANY);
    lv_obj_remove_flag(&component->base, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
        LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    return component;
}

static void component_destructor(const lv_obj_class_t *class_p, lv_obj_t *object)
{
    (void)class_p;
    component_t *component = (component_t *)object;
    for (component_t **link = &components; *link; link = &(*link)->next) {
        if (*link == component) { *link = component->next; break; }
    }
    /* 子对象已删除，GPU/LCD 已由页面退出前的安全点排空；字体延后统一 collect。 */
    (void)iw_font_release(&component->secondary);
    (void)iw_font_release(&component->primary);
    if (component->handle) component->handle->object = NULL;
}

static iw_component_result_t acquire_fonts(component_t *component)
{
    iw_font_id_t primary = iw_font_find(iw_theme_font_px(component->config.font_role));
    if (iw_font_acquire(primary, &component->primary) != IW_FONT_OK) return IW_COMPONENT_FONT_ERROR;
    if (component->kind == IW_LIST_ROW || component->kind == IW_STATE_PANEL || component->kind == IW_STATUS_BANNER) {
        if (iw_font_acquire(IW_FONT_22, &component->secondary) != IW_FONT_OK) return IW_COMPONENT_FONT_ERROR;
    }
    return IW_COMPONENT_OK;
}

static bool content_fits(const component_t *component, const iw_component_view_t *view)
{
    if (component->kind == IW_SCREEN_FRAME) return true;
    int32_t minimum = component->primary.font->line_height;
    if (view->detail && view->detail[0]) {
        const iw_theme_t *theme = iw_theme_get();
        minimum += component->secondary.font->line_height + theme->gap[1] + 2 * theme->card_padding;
    }
    return component->config.height >= minimum;
}

iw_component_result_t iw_screen_frame_create(iw_component_t *handle, lv_obj_t *parent,
    iw_frame_mode_t mode, const char *title, void (*quiesce)(void *), void *context)
{
    if (!iw_font_port_is_owner()) return IW_COMPONENT_BUSY;
    if (!handle || handle->object || !parent || mode < IW_FRAME_NORMAL || mode > IW_FRAME_FULLSCREEN ||
        !text_valid(title, TITLE_BYTES)) return IW_COMPONENT_INVALID;
    if (!iw_font_port_is_owner() || iw_gui_fault_pending() || !iw_font_port_render_idle()) return IW_COMPONENT_BUSY;
    const iw_theme_t *theme = iw_theme_get();
    iw_component_config_t config = {0};
    config.width = (int16_t)theme->width;
    config.height = (int16_t)theme->height;
    config.font_role = IW_THEME_FONT_TITLE;
    component_t *frame = create_object(handle, parent, IW_SCREEN_FRAME, &config);
    if (!frame) return IW_COMPONENT_NO_MEMORY;
    iw_component_result_t result = acquire_fonts(frame);
    if (result == IW_COMPONENT_OK && !iw_gui_owner_add(&frame->owner, stop_frame, handle)) result = IW_COMPONENT_BUSY;
    if (result != IW_COMPONENT_OK) { lv_obj_delete(&frame->base); return result; }
    frame->quiesce = quiesce;
    frame->page_context = context;
    copy_text(frame->title, title);
    bool full = mode == IW_FRAME_FULLSCREEN;
    config.x = full ? 0 : (int16_t)theme->inset_x;
    config.y = full ? 0 : (int16_t)(theme->inset_y + theme->font_px[IW_THEME_FONT_TITLE] + theme->gap[3]);
    config.width -= 2 * config.x;
    config.height -= config.y + (full ? 0 : (int16_t)theme->inset_y);
    /* 内层只提供裁剪及滚动，不绘字；固定标题不会随内容侵入安全区。 */
    component_t *content = create_object(&frame->content, &frame->base, IW_COMPONENT_KIND_COUNT, &config);
    if (!content) { lv_obj_delete(&frame->base); return IW_COMPONENT_NO_MEMORY; }
    if (mode == IW_FRAME_SCROLL) lv_obj_add_flag(&content->base, LV_OBJ_FLAG_SCROLLABLE);
    if (full) frame->title[0] = 0;
    return IW_COMPONENT_OK;
}

lv_obj_t *iw_screen_frame_content(const iw_component_t *handle)
{
    if (!iw_font_port_is_owner()) return NULL;
    component_t *frame = get(handle);
    return frame && frame->kind == IW_SCREEN_FRAME ? frame->content.object : NULL;
}

iw_component_result_t iw_component_create(iw_component_t *handle, lv_obj_t *parent,
    iw_component_kind_t kind, const iw_component_config_t *config, const iw_component_view_t *view)
{
    if (!iw_font_port_is_owner()) return IW_COMPONENT_BUSY;
    if (!handle || handle->object || !parent || !config || kind <= IW_SCREEN_FRAME || kind >= IW_COMPONENT_KIND_COUNT ||
        !view_valid(kind, view) || !iw_theme_effects(config->quality, config->reduced_motion) ||
        !iw_theme_font_px(config->font_role) || config->width < (int)iw_theme_get()->hit_size ||
        config->width > (int)iw_theme_get()->width || config->height < (int)iw_theme_get()->row_min_height ||
        config->height > (int)iw_theme_get()->height ||
        (kind == IW_PILL_BUTTON && config->height != (int)iw_theme_get()->button_height)) return IW_COMPONENT_INVALID;
    if (!iw_font_port_is_owner() || iw_gui_fault_pending() || !iw_font_port_render_idle()) return IW_COMPONENT_BUSY;
    lv_obj_t *ancestor = parent;
    while (ancestor && !(lv_obj_check_type(ancestor, &component_class) && ((component_t *)ancestor)->kind == IW_SCREEN_FRAME))
        ancestor = lv_obj_get_parent(ancestor);
    if (!ancestor) return IW_COMPONENT_INVALID;
    component_t *component = create_object(handle, parent, kind, config);
    if (!component) return IW_COMPONENT_NO_MEMORY;
    iw_component_result_t result = acquire_fonts(component);
    if (result != IW_COMPONENT_OK) { lv_obj_delete(&component->base); return result; }
    if (!content_fits(component, view)) { lv_obj_delete(&component->base); return IW_COMPONENT_INVALID; }
    copy_view(component, view);
    press_to(component, view->state == IW_PRESSED);
    return IW_COMPONENT_OK;
}

iw_component_result_t iw_component_update(iw_component_t *handle, const iw_component_view_t *view)
{
    if (!iw_font_port_is_owner() || iw_gui_fault_pending()) return IW_COMPONENT_BUSY;
    component_t *component = get(handle);
    if (!component || !view_valid(component->kind, view) || !content_fits(component, view)) return IW_COMPONENT_INVALID;
    /* 验证全部输入后一次提交；更新不申请字符串、样式或字体。 */
    copy_view(component, view);
    press_to(component, view->state == IW_PRESSED);
    return IW_COMPONENT_OK;
}

iw_component_result_t iw_component_destroy(iw_component_t *handle)
{
    if (!handle) return IW_COMPONENT_INVALID;
    if (!iw_font_port_is_owner()) return IW_COMPONENT_BUSY;
    if (!handle->object) return IW_COMPONENT_OK;
    if (!iw_font_port_render_idle()) return IW_COMPONENT_BUSY;
    if (!get(handle)) return IW_COMPONENT_INVALID;
    lv_obj_delete(handle->object);
    return IW_COMPONENT_OK;
}

bool iw_components_tick(void)
{
    if (!iw_font_port_is_owner() || iw_gui_fault_pending()) return false;
    bool active = false;
    for (component_t *component = components; component; component = component->next) {
        if (!component->animating) continue;
        const iw_theme_effects_t *effects = iw_theme_effects(component->config.quality, component->config.reduced_motion);
        uint32_t elapsed = lv_tick_elaps(component->press_started);
        if (elapsed >= effects->press_ms) {
            component->press_value = component->press_to;
            component->animating = false;
        }
        else {
            int32_t delta = (int32_t)component->press_to - component->press_from;
            component->press_value = (uint16_t)(component->press_from + delta * (int32_t)elapsed / effects->press_ms);
            active = true;
        }
        lv_obj_invalidate(&component->base);
    }
    return active;
}

static bool submit_fill(lv_layer_t *layer, const lv_area_t *area, uint32_t color, int32_t radius,
    uint8_t opacity, bool gradient, uint32_t end_color)
{
    lv_draw_task_t *task = lv_draw_add_task_checked(layer, area, LV_DRAW_TASK_TYPE_FILL);
    if (!task) { iw_gui_fault_raise(); return false; }
    lv_draw_fill_dsc_t *dsc = task->draw_dsc;
    lv_draw_fill_dsc_init(dsc);
    dsc->allocation_failed_cb = iw_gui_fault_raise;
    dsc->radius = radius;
    dsc->opa = opacity;
    dsc->color = lv_color_hex(color);
    if (gradient) {
        dsc->grad.dir = LV_GRAD_DIR_VER;
        dsc->grad.stops_count = 2;
        dsc->grad.stops[0].color = lv_color_hex(color);
        dsc->grad.stops[1].color = lv_color_hex(end_color);
        dsc->grad.stops[0].opa = dsc->grad.stops[1].opa = LV_OPA_COVER;
        dsc->grad.stops[1].frac = 255;
    }
    lv_draw_finalize_task_creation(layer, task);
    return true;
}

static bool submit_text(lv_layer_t *layer, const lv_area_t *area, const char *text,
    const lv_font_t *font, uint32_t color, lv_text_align_t align, uint8_t opacity)
{
    if (!text[0] || area->x2 < area->x1 || area->y2 < area->y1) return true;
    lv_area_t clip;
    if (!lv_area_intersect(&clip, &layer->_clip_area, area)) return true;
    /* 任务可能异步读取文案；独立快照避免下一次 view 更新改写在途内容。 */
    size_t bytes = strlen(text) + 1;
    char *snapshot = lv_malloc(bytes);
    if (!snapshot) { iw_gui_fault_raise(); return false; }
    lv_draw_task_t *task = lv_draw_add_task_checked(layer, area, LV_DRAW_TASK_TYPE_LABEL);
    if (!task) { lv_free(snapshot); iw_gui_fault_raise(); return false; }
    memcpy(snapshot, text, bytes);
    /* 文本严格裁剪到分配区域，长正文不能覆盖相邻行、右值或状态条。 */
    task->clip_area = clip;
    lv_draw_label_dsc_t *dsc = task->draw_dsc;
    lv_draw_label_dsc_init(dsc);
    dsc->text = snapshot;
    dsc->text_local = 1;
    dsc->font = font;
    dsc->color = lv_color_hex(color);
    dsc->align = align;
    dsc->opa = opacity;
    lv_draw_finalize_task_creation(layer, task);
    return !iw_gui_fault_pending();
}

static uint32_t tone_color(iw_component_state_t state)
{
    const iw_theme_t *theme = iw_theme_get();
    if (state == IW_ERROR || state == IW_DANGER) return theme->danger;
    if (state == IW_SUCCESS) return theme->success;
    if (state == IW_WARNING || state == IW_UNAVAILABLE) return theme->warning;
    return theme->accent;
}

static void draw_component(component_t *component, lv_layer_t *layer)
{
    if (iw_gui_fault_pending() || component->kind == IW_COMPONENT_KIND_COUNT) return;
    const iw_theme_t *theme = iw_theme_get();
    const iw_theme_effects_t *effects = iw_theme_effects(component->config.quality, component->config.reduced_motion);
    lv_area_t area = component->base.coords;
    uint32_t background = theme->surface;
    uint8_t opacity = component->state == IW_DISABLED ? LV_OPA_50 : LV_OPA_COVER;
    if (component->kind == IW_SCREEN_FRAME) {
        if (!submit_fill(layer, &area, theme->background, 0, 255, false, 0)) return;
        area.x1 += theme->inset_x; area.x2 -= theme->inset_x;
        area.y1 += theme->inset_y;
        area.y2 = area.y1 + component->primary.font->line_height - 1;
        (void)submit_text(layer, &area, component->title, component->primary.font, theme->text, LV_TEXT_ALIGN_LEFT, 255);
        return;
    }
    if (component->kind == IW_PILL_BUTTON) background = tone_color(component->state);
    if (component->pressed) background = theme->raised;
    int32_t inset_x = lv_area_get_width(&area) * (1000 - effects->pressed_scale_permille) * component->press_value / 2000000;
    int32_t inset_y = lv_area_get_height(&area) * (1000 - effects->pressed_scale_permille) * component->press_value / 2000000;
    area.x1 += inset_x; area.x2 -= inset_x; area.y1 += inset_y; area.y2 -= inset_y;
    int32_t radius = component->kind == IW_PILL_BUTTON ? theme->button_radius : theme->card_radius;
    if (!submit_fill(layer, &area, background, radius, effects->surface_opacity, false, 0)) return;
    if (component->pressed && !submit_fill(layer, &area, theme->text, radius, effects->highlight_opacity,
            effects->highlight_gradient, background)) return;
    area.x1 += theme->card_padding; area.x2 -= theme->card_padding;
    area.y1 += theme->card_padding; area.y2 -= theme->card_padding;
    int32_t line = component->primary.font->line_height;
    if (component->kind == IW_PILL_BUTTON) {
        area.y1 = (component->base.coords.y1 + component->base.coords.y2 - line + 1) / 2;
        area.y2 = area.y1 + line - 1;
        const char *text = component->state == IW_WAITING ? "..." : component->title;
        (void)submit_text(layer, &area, text, component->primary.font, theme->text, LV_TEXT_ALIGN_CENTER, opacity);
        return;
    }
    if (component->kind == IW_STATUS_BANNER || component->kind == IW_STATE_PANEL) {
        lv_area_t mark = area;
        mark.x2 = mark.x1 + theme->gap[0] - 1;
        if (!submit_fill(layer, &mark, tone_color(component->state), theme->gap[0] / 2, 255, false, 0)) return;
        area.x1 += theme->gap[2];
    }
    lv_area_t title = area;
    if (!component->detail[0]) title.y1 = (component->base.coords.y1 + component->base.coords.y2 - line + 1) / 2;
    title.y2 = title.y1 + line - 1;
    if (component->kind == IW_LIST_ROW && component->value[0]) {
        lv_area_t value = title;
        int32_t value_width = lv_area_get_width(&area) / 3;
        value.x1 = value.x2 - value_width + 1;
        title.x2 = value.x1 - theme->gap[1] - 1;
        if (!submit_text(layer, &value, component->value, component->primary.font, theme->secondary, LV_TEXT_ALIGN_RIGHT, opacity)) return;
    }
    if (!submit_text(layer, &title, component->title, component->primary.font, theme->text, LV_TEXT_ALIGN_LEFT, opacity)) return;
    if (component->detail[0]) {
        area.y1 = title.y2 + theme->gap[1] + 1;
        (void)submit_text(layer, &area, component->detail, component->secondary.font, theme->secondary, LV_TEXT_ALIGN_LEFT, opacity);
    }
}

static void component_event(const lv_obj_class_t *class_p, lv_event_t *event)
{
    (void)class_p;
    component_t *component = (component_t *)lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DRAW_MAIN) { draw_component(component, lv_event_get_layer(event)); return; }
    if (code == LV_EVENT_DRAW_POST) return;
    if (code == LV_EVENT_COVER_CHECK) { ((lv_cover_check_info_t *)lv_event_get_param(event))->res = LV_COVER_RES_NOT_COVER; return; }
    if (code == LV_EVENT_DELETE) {
        component->config.on_action = NULL;
        component->animating = false;
        (void)iw_gui_owner_remove(&component->owner);
        if (component->quiesce) {
            void (*quiesce)(void *) = component->quiesce;
            component->quiesce = NULL;
            quiesce(component->page_context);
        }
        return;
    }
    if (lv_obj_event_base(&component_class, event) != LV_RESULT_OK) return;
    if (code == LV_EVENT_PRESSED && interactive(component)) press_to(component, true);
    else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_RELEASED || code == LV_EVENT_INDEV_RESET)
        press_to(component, false);
    else if (code == LV_EVENT_CLICKED && interactive(component) && !iw_gui_fault_pending()) {
        if (component->config.on_action) component->config.on_action(component->config.action, component->config.context);
        /* 回调可能更新等待态或请求离开；此后不再访问 component。 */
    }
}
