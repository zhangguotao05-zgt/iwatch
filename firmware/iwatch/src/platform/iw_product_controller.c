#include "iw_product_controller.h"
#include "iw_service_runtime.h"
#include "iw_ui_commands.h"
#include "iw_font_port.h"
#include "iw_gui_owner.h"
#include "iw_gui_port.h"
#include <string.h>
#ifdef IW_TARGET_BUILD
#include "iw_build_info.h"
#else
#define IW_BUILD_TAG "HOST TEST"
#endif

static const iw_ui_command_port_t command_port = {iw_request_allocate, iw_service_current_session,
                                                  iw_command_submit, iw_result_get, iw_result_ack};
static iw_ui_commands_t commands = {.port = &command_port};
static iw_product_page_t *pages;
static iw_theme_quality_t profile_quality = IW_THEME_Q1;
static bool profile_large, profile_reduced;

bool iw_product_set_profile(iw_theme_quality_t quality, bool large_text, bool reduced_motion) {
    if (!iw_font_port_is_owner() || !iw_theme_effects(quality, reduced_motion)) return false;
    profile_quality = quality;
    profile_large = large_text;
    profile_reduced = reduced_motion;
    for (iw_product_page_t *p = pages; p; p = p->next) {
        p->model.quality = quality;
        p->model.large_text = large_text;
        p->model.reduced_motion = reduced_motion;
        iw_product_view_activate(&p->view, p->visible);
        p->dirty = true;
    }
    return true;
}

static void stop(void *context) {
    iw_product_page_t *p = context;
    p->visible = false;
    p->exiting = true;
    p->queued_level = false;
    iw_ui_commands_detach(&commands, p->page_id, p->generation);
    if (p->quiesce) p->quiesce(p->context);
}

static void capabilities(iw_product_model_t *m) {
    struct {
        iw_snapshot_header_t header;
        iw_capability_snapshot_t model;
    } value = {0};
    size_t bytes = 0;
    bool clock_ready = false, rtc_ready = false;
    m->display_available = m->time_available = false;
    if (iw_snapshot_read(IW_SNAPSHOT_CAPABILITIES, &value, sizeof(value), &bytes) != IW_SNAPSHOT_OK ||
        value.model.count > IW_CAPABILITY_CAPACITY ||
        bytes != sizeof(value.header) + offsetof(iw_capability_snapshot_t, entries) +
                     value.model.count * sizeof(value.model.entries[0]))
        return;
    for (unsigned i = 0; i < value.model.count; i++) {
        const iw_capability_entry_t *e = &value.model.entries[i];
        /* 尚未校准属于降级，仍允许写入；RTC 缺失或故障时不能开放编辑。 */
        bool usable = e->state == IW_CAP_STATE_AVAILABLE || e->state == IW_CAP_STATE_DEGRADED;
        if (e->capability_id == IW_CAP_CLOCK) clock_ready = usable;
        if (e->capability_id == IW_CAP_RTC_BACKUP) rtc_ready = usable;
        if (e->capability_id == IW_CAP_DISPLAY)
            m->display_available = e->state == IW_CAP_STATE_AVAILABLE;
    }
    m->time_available = clock_ready && rtc_ready;
}

static bool send(iw_product_page_t *p, iw_command_t *command) {
    iw_submit_status_t status = iw_ui_command_send(&commands, command, lv_tick_get(), &p->request);
    if (status != IW_SUBMIT_QUEUED && status != IW_SUBMIT_DUPLICATE) {
        p->model.message = status == IW_SUBMIT_CAPABILITY_UNAVAILABLE
            ? (p->page_id == IW_PAGE_TIME ? IW_TEXT_TIME_FAILED : IW_TEXT_DISPLAY_UNAVAILABLE)
            : IW_TEXT_BUSY;
        p->model.preview_level = 0;
        p->dirty = true;
        return false;
    }
    p->session = command->session_id;
    p->model.pending = true;
    p->model.message = IW_TEXT_COUNT;
    p->dirty = true;
    return true;
}

static void submit_clock_edit(iw_product_page_t *p) {
    if (p->request) return;
    capabilities(&p->model);
    if (!p->model.time_available) {
        p->model.message = IW_TEXT_TIME_FAILED;
        p->dirty = true;
        return;
    }
    int64_t utc;
    iw_draft_status_t status = iw_time_draft_utc(&p->model.draft, &utc);
    if (status != IW_DRAFT_OK) {
        p->model.message = status == IW_DRAFT_INVALID_DATE ? IW_TEXT_INVALID_DATE : IW_TEXT_TIME_RANGE;
        p->dirty = true;
        return;
    }
    iw_command_t command;
    iw_command_init(&command, 1, 1, p->page_id, p->generation, IW_OPCODE_SET_CLOCK);
    if (iw_command_encode_set_clock(&command, (uint32_t)utc, p->model.draft.offset_minutes,
                                    p->model.draft.expected_revision))
        (void)send(p, &command);
}

static void action(uint16_t id, int32_t value, bool final, void *context) {
    iw_product_page_t *p = context;
    if (!p->visible || p->exiting || iw_gui_fault_pending()) return;
    if (id == IW_ACTION_BACK) {
        if (!iw_product_back(p)) p->navigate(id, p->context);
        return;
    }
    if (id == IW_ACTION_TIME_CANCEL) {
        p->navigate(IW_ACTION_BACK, p->context);
        return;
    }
    if (id == IW_ACTION_TIME_SAVE) {
        submit_clock_edit(p);
        return;
    }
    if (id == IW_ACTION_RELOAD) {
        if (!p->request && iw_clock_read(&p->model.clock) &&
            iw_time_draft_begin(&p->model.draft, &p->model.clock))
            p->model.message = IW_TEXT_COUNT;
    } else if (id >= IW_ACTION_FIELD && id < IW_ACTION_FIELD + IW_EDIT_NONE) {
        if (!p->request) (void)iw_time_draft_select(&p->model.draft, (iw_time_field_t)(id - IW_ACTION_FIELD));
    } else if (id == IW_ACTION_PICK_STEP) {
        if (!p->request && value >= -1 && value <= 1)
            (void)iw_time_draft_step(&p->model.draft, value);
    } else if (id == IW_ACTION_PICK_PREV || id == IW_ACTION_PICK_NEXT) {
        if (!p->request) (void)iw_time_draft_step(&p->model.draft, id == IW_ACTION_PICK_PREV ? -1 : 1);
    } else if (id == IW_ACTION_PICK_CANCEL)
        iw_time_draft_cancel_field(&p->model.draft);
    else if (id == IW_ACTION_PICK_CHOOSE)
        (void)iw_time_draft_choose(&p->model.draft);
    else if (id == IW_ACTION_DIM || id == IW_ACTION_BRIGHTEN || id == IW_ACTION_TRACK) {
        if (!p->model.display_available) return;
        uint8_t base = p->queued_level          ? p->next_level
                       : p->model.preview_level ? p->model.preview_level
                                                : p->model.brightness.desired;
        /* 最后一次松手不会被尚未消费的拖动预览覆盖。 */
        if (!p->final_level || final) {
            p->next_level = id == IW_ACTION_TRACK
                                ? (uint8_t)value
                                : iw_brightness_step_level(base, id == IW_ACTION_DIM ? -5 : 5);
            p->queued_level = true;
            p->final_level = final;
        }
        iw_gui_wake(IW_GUI_WAKE_STATE);
    } else {
        p->navigate(id, p->context);
        return;
    }
    p->dirty = true;
}

bool iw_product_create(iw_product_page_t *p, uint16_t id, uint32_t generation, bool back,
                       void (*navigate)(uint16_t, void *), void (*quiesce)(void *), void *context) {
    if (!p || p->linked || !generation || !navigate || !iw_font_port_is_owner()) return false;
    p->page_id = id;
    p->generation = generation;
    p->navigate = navigate;
    p->quiesce = quiesce;
    p->context = context;
    p->model = (iw_product_model_t){.message = IW_TEXT_COUNT, .back = back,
        .hardware = "SF32LB58 A128 QSPI", .firmware = IW_BUILD_TAG, .quality = profile_quality,
        .large_text = profile_large, .reduced_motion = profile_reduced};
#if defined(__ARMCOMPILER_VERSION)
    p->model.toolchain = "Arm Compiler " __VERSION__;
#elif defined(__GNUC__)
    p->model.toolchain = "GCC " __VERSION__;
#endif
    (void)iw_clock_read(&p->model.clock);
    (void)iw_brightness_read(&p->model.brightness);
    capabilities(&p->model);
    if (id == IW_PAGE_TIME && !iw_time_draft_begin(&p->model.draft, &p->model.clock)) return false;
    if (id == IW_PAGE_TIME && p->model.draft.needs_calibration) p->model.message = IW_TEXT_CALIBRATE;
    if (id != IW_PAGE_TIME) p->model.draft.editing = IW_EDIT_NONE;
    p->exiting = false;
    if (!iw_product_view_create(&p->view, lv_screen_active(), id, &p->model, action, stop, p)) return false;
    p->next = pages;
    pages = p;
    p->linked = true;
    p->visible = true;
    p->last_poll = lv_tick_get();
    return true;
}

void iw_product_resume(iw_product_page_t *p, bool visible) {
    if (!p || !iw_font_port_is_owner()) return;
    p->visible = visible;
    iw_product_view_activate(&p->view, visible);
    p->dirty = true;
}

bool iw_product_destroy(iw_product_page_t *p) {
    if (!p || !iw_font_port_is_owner() || !iw_font_port_render_idle()) return false;
    if (!iw_product_view_destroy(&p->view)) return false;
    iw_ui_commands_detach(&commands, p->page_id, p->generation);
    for (iw_product_page_t **link = &pages; *link; link = &(*link)->next)
        if (*link == p) {
            *link = p->next;
            break;
        }
    p->linked = false;
    p->next = NULL;
    p->visible = false;
    return true;
}

bool iw_product_back(iw_product_page_t *p) {
    if (p && p->page_id == IW_PAGE_TIME && p->model.draft.editing != IW_EDIT_NONE) {
        iw_time_draft_cancel_field(&p->model.draft);
        p->dirty = true;
        return true;
    }
    return false;
}

bool iw_product_rotate(iw_product_page_t *p, int32_t steps) {
    if (!p || !p->visible || p->exiting) return false;
    if (steps > 32) steps = 32;
    if (steps < -32) steps = -32;
    if (p->page_id == IW_PAGE_TIME && p->model.draft.editing != IW_EDIT_NONE) {
        if (!p->request) (void)iw_time_draft_step(&p->model.draft, steps);
        p->dirty = true;
    } else if (p->page_id == IW_PAGE_BRIGHTNESS) {
        uint8_t base = p->queued_level ? p->next_level : p->model.brightness.desired;
        action(IW_ACTION_TRACK, iw_brightness_step_level(base, steps), true, p);
    } else
        iw_product_view_scroll(&p->view, steps * 24);
    return true;
}

uint32_t iw_product_process(void) {
    if (!iw_font_port_is_owner()) return UINT32_MAX;
    iw_ui_commands_poll(&commands);
    uint32_t now = lv_tick_get(), wait = iw_ui_commands_pending(&commands) ? 50u : UINT32_MAX;
    for (iw_product_page_t *p = pages; p; p = p->next) {
        if (p->exiting) continue;
        iw_result_t result;
        if (p->request && iw_ui_command_take(&commands, p->session, p->request, &result)) {
            p->request = 0;
            p->model.pending = false;
            p->model.preview_level = 0;
            p->dirty = true;
            if (result.code == IW_RESULT_OK_APPLIED || result.code == IW_RESULT_OK_PERSISTED ||
                result.code == IW_RESULT_SUPERSEDED) {
                p->model.message = IW_TEXT_COUNT;
                if (p->page_id == IW_PAGE_TIME && result.code == IW_RESULT_OK_APPLIED && p->visible)
                    p->navigate(IW_ACTION_BACK, p->context);
            } else
                p->model.message = result.code == IW_RESULT_STATE_CONFLICT && p->page_id == IW_PAGE_TIME ? IW_TEXT_TIME_CONFLICT
                                   : p->page_id == IW_PAGE_TIME            ? IW_TEXT_TIME_FAILED
                                                                           : IW_TEXT_BRIGHTNESS_FAILED;
        } else if (p->request && iw_ui_command_delayed(&commands, p->session, p->request, now)) {
            if (p->model.message != IW_TEXT_CONFIRMING) {
                p->model.message = IW_TEXT_CONFIRMING;
                p->dirty = true;
            }
        }
        if (p->queued_level && !p->request && (p->final_level || (uint32_t)(now - p->last_preview) >= 50u)) {
            iw_brightness_snapshot_t b;
            if (iw_brightness_read(&b) && b.setting_sequence != UINT32_MAX) {
                iw_command_t command;
                iw_command_init(&command, 1, 1, p->page_id, p->generation, IW_OPCODE_SET_BRIGHTNESS);
                bool encoded = iw_command_encode_set_brightness(
                    &command, p->next_level, p->final_level ? IW_BRIGHTNESS_FINAL : IW_BRIGHTNESS_PREVIEW,
                    b.desired_revision, b.setting_sequence + 1);
                if (encoded && send(p, &command)) p->model.preview_level = p->next_level;
            } else {
                p->model.message = IW_TEXT_BRIGHTNESS_FAILED;
                p->dirty = true;
            }
            p->queued_level = false;
            p->final_level = false;
            p->last_preview = now;
        }
        if (!p->visible) continue;
        uint32_t view_wait = iw_product_view_tick(&p->view);
        if (wait > 50u) wait = 50u;
        if (view_wait < wait) wait = view_wait;
        if ((uint32_t)(now - p->last_poll) >= 1000u) {
            (void)iw_clock_read(&p->model.clock);
            (void)iw_brightness_read(&p->model.brightness);
            capabilities(&p->model);
            p->last_poll = now;
            p->dirty = true;
        }
        if (p->dirty && iw_font_port_render_idle() && !iw_gui_fault_pending()) {
            (void)iw_brightness_read(&p->model.brightness);
            if (iw_product_view_update(&p->view, &p->model)) p->dirty = false;
        }
    }
    return wait;
}
