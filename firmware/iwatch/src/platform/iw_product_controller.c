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
static iw_notification_store_t notification_store;
#ifdef _WIN32
static bool lock_available = true;
#else
static bool lock_available;
#endif

static void notification_projection(iw_product_model_t *model)
{
    memset(model->notification_ids, 0, sizeof(model->notification_ids));
    for (unsigned i = 0; i < IW_NOTIFICATION_CAPACITY && i < notification_store.count; i++)
        model->notification_ids[i] = notification_store.records[notification_store.count - 1u - i].id;
}

bool iw_product_notification_add(iw_notification_source_t source,
                                 const char *text, size_t bytes)
{
    if (!iw_font_port_is_owner()) return false;
    iw_clock_snapshot_t clock;
    bool time_valid = iw_clock_read(&clock) && clock.valid && clock.utc_ms >= 0 &&
                      clock.utc_ms / 1000 <= UINT32_MAX;
    if (iw_notification_insert(&notification_store, source, text, bytes, time_valid,
                               time_valid ? (uint32_t)(clock.utc_ms / 1000) : 0u,
                               NULL) != IW_NOTIFICATION_OK) return false;
    for (iw_product_page_t *p = pages; p; p = p->next)
        if (p->page_id == IW_PAGE_NOTIFICATION_LIST) {
            notification_projection(&p->model);
            p->dirty = true;
        }
    return true;
}

void iw_product_set_recent(iw_product_page_t *page, const iw_recent_apps_t *recent)
{
    if (!page || !iw_font_port_is_owner()) return;
    page->model.recent_apps = recent;
    if (!recent || !recent->count) page->model.recent_index = 0u;
    else if (page->model.recent_index >= recent->count)
        page->model.recent_index = (uint8_t)(recent->count - 1u);
    page->dirty = true;
}

void iw_product_set_lock_progress(uint8_t progress)
{
    if (!iw_font_port_is_owner()) return;
    if (progress > 100u) progress = 100u;
    for (iw_product_page_t *p = pages; p; p = p->next)
        if (p->visible && (p->page_id == IW_PAGE_LOCK || p->page_id == IW_PAGE_WATER_LOCK) &&
            p->model.lock_progress != progress) {
            p->model.lock_progress = progress;
            p->dirty = true;
        }
}

void iw_product_set_lock_available(bool available)
{
    lock_available = available;
    if (!iw_font_port_is_owner()) return;
    for (iw_product_page_t *p = pages; p; p = p->next)
        if (p->model.lock_available != available) {
            p->model.lock_available = available;
            p->dirty = true;
        }
}

static void refresh_notification_pages(void)
{
    for (iw_product_page_t *p = pages; p; p = p->next)
        if (p->page_id == IW_PAGE_NOTIFICATION_LIST) {
            notification_projection(&p->model);
            p->dirty = true;
        }
}
static iw_theme_quality_t profile_quality = IW_THEME_Q1;
static bool profile_large, profile_reduced;
static void capabilities(iw_product_model_t *m);
/* 表盘配置只属于 GUI 会话；D15 再决定是否增加持久化回执。 */
static iw_face_session_t face_session = {
    .schema = IW_FACE_SCHEMA,
    .active_face_id = IW_FACE_DIGITAL,
    .revision = 1u,
    .color = IW_FACE_COLOR_BLUE,
    .center = IW_FACE_CENTER_NONE,
    .left = IW_FACE_LEFT_SETTINGS,
    .right = IW_FACE_RIGHT_ABOUT
};
static iw_face_session_t face_pending;
static iw_face_session_t face_previous;
static bool face_pending_valid;
static bool face_pending_committed;

static bool face_session_valid(const iw_face_session_t *value)
{
    return value && value->schema == IW_FACE_SCHEMA &&
           (value->active_face_id == IW_FACE_DIGITAL || value->active_face_id == IW_FACE_MODULAR_LOCAL) &&
           value->color < IW_FACE_COLOR_COUNT && value->center < IW_FACE_CENTER_COUNT &&
           value->left < IW_FACE_LEFT_COUNT && value->right < IW_FACE_RIGHT_COUNT &&
           (value->active_face_id == IW_FACE_MODULAR_LOCAL ||
            (value->center == IW_FACE_CENTER_NONE && value->left == IW_FACE_LEFT_SETTINGS &&
             value->right == IW_FACE_RIGHT_ABOUT));
}

static bool face_draft_apply(iw_product_page_t *p)
{
    iw_face_session_t candidate;
    if (!p || p->page_id != IW_PAGE_FACE_EDITOR || !p->model.face_draft.valid) return false;
    candidate = p->model.face_draft.value;
    if (face_pending_valid ||
        p->model.face_draft.expected_revision != face_session.revision ||
        face_session.revision == UINT32_MAX || !face_session_valid(&candidate)) {
        p->model.message = IW_TEXT_TIME_CONFLICT;
        p->dirty = true;
        return false;
    }
    capabilities(&p->model);
    if (!p->model.display_available) {
        p->model.message = IW_TEXT_DISPLAY_UNAVAILABLE;
        p->dirty = true;
        return false;
    }
    /* 候选只使用固定枚举和现有根页资源，不分配整屏预览缓冲。 */
    candidate.revision = face_session.revision + 1u;
    face_pending = candidate;
    face_previous = face_session;
    face_pending_valid = true;
    face_pending_committed = false;
    p->navigate(IW_ACTION_FACE_APPLY, 0u, p->context);
    return true;
}

bool iw_product_face_commit_pending(void)
{
    if (!face_pending_valid) return false;
    face_session = face_pending;
    face_pending_committed = true;
    face_pending_valid = false;
    return true;
}

void iw_product_face_cancel_pending(void)
{
    face_pending_valid = false;
    face_pending_committed = false;
}

void iw_product_face_rollback_pending(void)
{
    if (face_pending_committed) face_session = face_previous;
    iw_product_face_cancel_pending();
}

/* 叠放只绑定快照中的稳定 ID；数组顺序变化不能改变当前卡片的实体。 */
static uint32_t select_running_timer(const iw_timer_snapshot_t *snapshot)
{
    const iw_timer_view_t *best = NULL;
    if (!snapshot) return 0u;
    for (unsigned i = 0; i < snapshot->count; i++) {
        const iw_timer_view_t *candidate = &snapshot->timers[i];
        if (candidate->state != IW_TIMER_RUNNING || !candidate->timer_id) continue;
        if (!best || candidate->remaining_ms < best->remaining_ms ||
            (candidate->remaining_ms == best->remaining_ms &&
             candidate->timer_id < best->timer_id)) best = candidate;
    }
    return best ? best->timer_id : 0u;
}

static uint32_t select_next_alarm(const iw_alarm_snapshot_t *snapshot)
{
    const iw_alarm_t *best = NULL;
    if (!snapshot) return 0u;
    for (unsigned i = 0; i < snapshot->count; i++) {
        const iw_alarm_t *candidate = &snapshot->alarms[i];
        if (!candidate->enabled || !candidate->alarm_id || !candidate->next_due_utc_ms) continue;
        if (!best || candidate->next_due_utc_ms < best->next_due_utc_ms ||
            (candidate->next_due_utc_ms == best->next_due_utc_ms &&
             candidate->alarm_id < best->alarm_id)) best = candidate;
    }
    return best ? best->alarm_id : 0u;
}

static const iw_timer_view_t *find_timer(const iw_timer_snapshot_t *snapshot, uint32_t timer_id)
{
    if (!snapshot || !timer_id) return NULL;
    for (unsigned i = 0; i < snapshot->count; i++)
        if (snapshot->timers[i].timer_id == timer_id) return &snapshot->timers[i];
    return NULL;
}

static const iw_alarm_t *find_alarm(const iw_alarm_snapshot_t *snapshot, uint32_t alarm_id)
{
    if (!snapshot || !alarm_id) return NULL;
    for (unsigned i = 0; i < snapshot->count; i++)
        if (snapshot->alarms[i].alarm_id == alarm_id) return &snapshot->alarms[i];
    return NULL;
}

static void chronographs(iw_product_page_t *p) {
    struct {
        iw_snapshot_header_t header;
        iw_timer_snapshot_t model;
    } timers = {0};
    struct {
        iw_snapshot_header_t header;
        iw_stopwatch_snapshot_t model;
    } stopwatch = {0};
    size_t bytes = 0;
    p->model.stack_timer_id = 0u;
    if (iw_snapshot_read(IW_SNAPSHOT_TIMERS, &timers, sizeof(timers), &bytes) == IW_SNAPSHOT_OK &&
        timers.model.count <= IW_TIMER_CAPACITY) {
        p->model.timers = timers.model;
        p->model.stack_timer_id = select_running_timer(&timers.model);
        if (p->page_id == IW_PAGE_TIMER_LIST && p->model.message == IW_TEXT_TIMERS_FULL &&
            timers.model.count < IW_TIMER_CAPACITY)
            p->model.message = IW_TEXT_COUNT;
        memset(&p->model.selected_timer, 0, sizeof(p->model.selected_timer));
        for (unsigned i = 0; i < timers.model.count; i++)
            if (timers.model.timers[i].timer_id == p->argument)
                p->model.selected_timer = timers.model.timers[i];
    }
    bytes = 0;
    if (iw_snapshot_read(IW_SNAPSHOT_STOPWATCH, &stopwatch, sizeof(stopwatch), &bytes) == IW_SNAPSHOT_OK &&
        stopwatch.model.lap_count <= IW_STOPWATCH_LAP_CAPACITY) {
        if (p->page_id == IW_PAGE_STOPWATCH && p->model.message == IW_TEXT_LAPS_FULL &&
            (stopwatch.model.state != IW_STOPWATCH_RUNNING ||
             stopwatch.model.lap_count < IW_STOPWATCH_LAP_CAPACITY))
            p->model.message = IW_TEXT_COUNT;
        p->model.stopwatch.revision = stopwatch.model.revision;
        p->model.stopwatch.lap_count = stopwatch.model.lap_count;
        p->model.stopwatch.state = stopwatch.model.state;
        p->model.stopwatch.elapsed_ms = stopwatch.model.elapsed_ms;
        p->model.stopwatch.visible_laps = stopwatch.model.lap_count > 8u ? 8u :
                                            (uint8_t)stopwatch.model.lap_count;
        for (unsigned i = 0; i < p->model.stopwatch.visible_laps; i++)
            p->model.stopwatch.laps[i] = stopwatch.model.laps[stopwatch.model.lap_count - 1u - i];
    }
}

static void alarms_alerts(iw_product_page_t *p) {
    size_t bytes = 0;
    p->model.stack_alarm_id = 0u;
    if (p->page_id == IW_PAGE_ALARM_LIST || p->page_id == IW_PAGE_ALARM_EDIT ||
        p->page_id == IW_PAGE_SMART_STACK || p->page_id == IW_PAGE_FACE) {
        struct { iw_snapshot_header_t header; iw_alarm_snapshot_t model; } value = {0};
        if (iw_snapshot_read(IW_SNAPSHOT_ALARMS, &value, sizeof(value), &bytes) == IW_SNAPSHOT_OK &&
            value.model.count <= IW_ALARM_CAPACITY) {
            p->model.alarms = value.model;
            p->model.stack_alarm_id = select_next_alarm(&value.model);
            if (p->page_id == IW_PAGE_ALARM_LIST && p->model.message == IW_TEXT_ALARMS_FULL &&
                value.model.count < IW_ALARM_CAPACITY) p->model.message = IW_TEXT_COUNT;
        }
    }
    if (p->page_id == IW_PAGE_ALERT_TIMER || p->page_id == IW_PAGE_ALERT_ALARM ||
        p->page_id == IW_PAGE_LAUNCHER_LIST || p->page_id == IW_PAGE_NOTIFICATION_LIST) {
        struct { iw_snapshot_header_t header; iw_alert_snapshot_t model; } value = {0};
        bool list_page = p->page_id == IW_PAGE_LAUNCHER_LIST ||
                         p->page_id == IW_PAGE_NOTIFICATION_LIST;
        iw_alert_source_t source = p->page_id == IW_PAGE_ALERT_TIMER ?
                                   IW_ALERT_SOURCE_TIMER : IW_ALERT_SOURCE_ALARM;
        bytes = 0;
        if (iw_snapshot_read(IW_SNAPSHOT_ALERTS, &value, sizeof(value), &bytes) == IW_SNAPSHOT_OK &&
            value.model.count <= IW_ALERT_CAPACITY) {
            p->model.alerts = value.model;
            memset(&p->model.selected_alert, 0, sizeof(p->model.selected_alert));
            for (unsigned i = 0; i < value.model.count; i++) {
                const iw_alert_record_t *record = &value.model.records[i];
                if (list_page) {
                    if (!p->model.selected_alert.entity_id &&
                        record->state != IW_ALERT_SNOOZED)
                        p->model.selected_alert = *record;
                } else if (record->source_type == source &&
                           record->entity_id == p->argument)
                    p->model.selected_alert = *record;
            }
        }
    }
}

/* 点击前重新取实体，避免页面缓存把已删除或已失效对象当成有效目标。 */
static void refresh_action_snapshots(iw_product_page_t *p) {
    p->model.timers.count = 0;
    p->model.alarms.count = 0;
    p->model.selected_timer = (iw_timer_view_t){0};
    p->model.selected_alert = (iw_alert_record_t){0};
    p->model.stack_timer_id = 0u;
    p->model.stack_alarm_id = 0u;
    chronographs(p);
    alarms_alerts(p);
}

static bool alert_same(const iw_alert_record_t *a, const iw_alert_record_t *b) {
    return a && b && a->entity_id && a->source_type == b->source_type &&
           a->entity_id == b->entity_id && a->occurrence == b->occurrence &&
           a->presentation_epoch == b->presentation_epoch && a->state == b->state;
}

/* 将实体失效映射到用户能采取行动的具体提示，避免用通用错误掩盖状态变化。 */
static iw_product_text_id_t entity_changed_text(uint16_t page_id)
{
    switch (page_id) {
    case IW_PAGE_TIMER_LIST:
    case IW_PAGE_TIMER_DETAIL:
    case IW_PAGE_ALERT_TIMER:
        return IW_TEXT_TIMER_CHANGED;
    case IW_PAGE_ALARM_LIST:
    case IW_PAGE_ALARM_EDIT:
    case IW_PAGE_ALERT_ALARM:
        return IW_TEXT_ALARM_CHANGED;
    default:
        return IW_TEXT_OPERATION_FAILED;
    }
}

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

static void submit_timer_create(iw_product_page_t *p, uint32_t duration_ms) {
    if (p->request) return;
    iw_command_t command;
    iw_command_init(&command, 1u, 1u, p->page_id, p->generation, IW_OPCODE_TIMER_CREATE);
    if (!iw_command_encode_timer_create(&command, duration_ms) || !send(p, &command))
        p->model.message = IW_TEXT_OPERATION_FAILED;
}

static void submit_timer_control(iw_product_page_t *p, iw_opcode_t opcode) {
    if (p->request || !p->model.selected_timer.timer_id) return;
    iw_command_t command;
    iw_command_init(&command, 1u, 1u, p->page_id, p->generation, opcode);
    if (!iw_command_encode_timer_control(&command, p->model.selected_timer.timer_id,
                                         p->model.selected_timer.revision) || !send(p, &command))
        p->model.message = IW_TEXT_OPERATION_FAILED;
}

static void submit_stopwatch(iw_product_page_t *p, iw_opcode_t opcode) {
    if (p->request) return;
    iw_command_t command;
    iw_command_init(&command, 1u, 1u, p->page_id, p->generation, opcode);
    if (!iw_command_encode_stopwatch_control(&command, p->model.stopwatch.revision) ||
        !send(p, &command))
        p->model.message = IW_TEXT_OPERATION_FAILED;
}

static void submit_alarm_apply(iw_product_page_t *p) {
    iw_command_t command;
    uint32_t handle;
    if (p->request || !iw_alarm_draft_runtime_store(&p->model.alarm_edit, &handle)) {
        p->model.message = IW_TEXT_OPERATION_FAILED;
        return;
    }
    iw_command_init(&command, 1u, 1u, p->page_id, p->generation, IW_OPCODE_ALARM_APPLY);
    if (!iw_command_encode_alarm_apply(&command, handle,
                                       p->model.alarm_edit.expected_revision) ||
        !send(p, &command)) {
        (void)iw_alarm_draft_runtime_discard(handle);
        p->model.message = IW_TEXT_OPERATION_FAILED;
    }
}

static void submit_alarm_delete(iw_product_page_t *p) {
    iw_command_t command;
    if (p->request || !p->model.alarm_edit.alarm_id) return;
    iw_command_init(&command, 1u, 1u, p->page_id, p->generation, IW_OPCODE_ALARM_DELETE);
    if (!iw_command_encode_alarm_delete(&command, p->model.alarm_edit.alarm_id,
                                        p->model.alarm_edit.expected_revision) || !send(p, &command))
        p->model.message = IW_TEXT_OPERATION_FAILED;
}

static void submit_alert_control(iw_product_page_t *p, iw_opcode_t opcode) {
    const iw_alert_record_t *record = &p->model.selected_alert;
    iw_command_t command;
    if (p->request || !record->entity_id || !record->occurrence) return;
    iw_command_init(&command, 1u, 1u, p->page_id, p->generation, opcode);
    if (!iw_command_encode_alert_control(&command,
                                         (iw_alert_source_t)record->source_type,
                                         record->entity_id, record->occurrence) || !send(p, &command))
        p->model.message = IW_TEXT_OPERATION_FAILED;
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
        if (!iw_product_back(p)) p->navigate(id, 0u, p->context);
        return;
    }
    if (id == IW_ACTION_TIME_CANCEL) {
        p->navigate(IW_ACTION_BACK, 0u, p->context);
        return;
    }
    if (id == IW_ACTION_FACE_NOTIFICATIONS || id == IW_ACTION_FACE_STACK) {
        p->navigate(id == IW_ACTION_FACE_NOTIFICATIONS ? IW_PAGE_NOTIFICATION_LIST :
                    IW_PAGE_SMART_STACK, 0u, p->context);
        return;
    }
    if (id == IW_ACTION_FACE_PICKER) {
        p->navigate(IW_PAGE_FACE_PICKER, 0u, p->context);
        return;
    }
    if (id >= IW_ACTION_FACE_EDITOR_OPEN_BASE &&
        id < IW_ACTION_FACE_EDITOR_OPEN_BASE + IW_FACE_MODULAR_LOCAL + 1u) {
        uint16_t face_id = (uint16_t)(id - IW_ACTION_FACE_EDITOR_OPEN_BASE);
        if (face_id == IW_FACE_DIGITAL || face_id == IW_FACE_MODULAR_LOCAL)
            p->navigate(IW_PAGE_FACE_EDITOR, face_id, p->context);
        return;
    }
    if (id == IW_ACTION_FACE_CANCEL) {
        p->navigate(IW_ACTION_BACK, 0u, p->context);
        return;
    }
    if (id == IW_ACTION_FACE_APPLY) {
        (void)face_draft_apply(p);
        return;
    }
    if (p->page_id == IW_PAGE_FACE_EDITOR &&
        (id == IW_ACTION_FACE_COLOR || id == IW_ACTION_FACE_CENTER ||
         id == IW_ACTION_FACE_LEFT || id == IW_ACTION_FACE_RIGHT)) {
        iw_face_session_t *draft = &p->model.face_draft.value;
        if (!p->model.face_draft.valid) return;
        if (id == IW_ACTION_FACE_COLOR)
            draft->color = (uint8_t)((draft->color + 1u) % IW_FACE_COLOR_COUNT);
        else if (id == IW_ACTION_FACE_CENTER && draft->active_face_id == IW_FACE_MODULAR_LOCAL)
            draft->center = (uint8_t)((draft->center + 1u) % IW_FACE_CENTER_COUNT);
        else if (id == IW_ACTION_FACE_LEFT && draft->active_face_id == IW_FACE_MODULAR_LOCAL)
            draft->left = (uint8_t)((draft->left + 1u) % IW_FACE_LEFT_COUNT);
        else if (id == IW_ACTION_FACE_RIGHT && draft->active_face_id == IW_FACE_MODULAR_LOCAL)
            draft->right = (uint8_t)((draft->right + 1u) % IW_FACE_RIGHT_COUNT);
        p->dirty = true;
        return;
    }
    if (id == IW_ACTION_TIME_SAVE) {
        submit_clock_edit(p);
        return;
    }
    if (id == IW_ACTION_ALARM_ADD) {
        p->navigate(IW_PAGE_ALARM_EDIT, 0u, p->context);
        return;
    }
    if (id >= IW_ACTION_ALARM_OPEN_BASE && id < IW_ACTION_ALARM_OPEN_BASE + IW_ALARM_CAPACITY) {
        unsigned index = id - IW_ACTION_ALARM_OPEN_BASE;
        uint32_t expected_id = index < p->model.alarms.count ?
                               p->model.alarms.alarms[index].alarm_id : 0u;
        refresh_action_snapshots(p);
        if (expected_id && index < p->model.alarms.count &&
            p->model.alarms.alarms[index].alarm_id == expected_id)
            p->navigate(IW_PAGE_ALARM_EDIT, expected_id, p->context);
        else {
            p->model.message = IW_TEXT_ALARM_CHANGED;
            p->dirty = true;
        }
        return;
    }
    if (id >= IW_ACTION_ALARM_HOUR_MINUS && id <= IW_ACTION_ALARM_MINUTE_PLUS) {
        iw_alarm_edit_t *edit = &p->model.alarm_edit;
        if (id == IW_ACTION_ALARM_HOUR_MINUS) edit->hour = (uint8_t)((edit->hour + 23u) % 24u);
        else if (id == IW_ACTION_ALARM_HOUR_PLUS) edit->hour = (uint8_t)((edit->hour + 1u) % 24u);
        else if (id == IW_ACTION_ALARM_MINUTE_MINUS) edit->minute = (uint8_t)((edit->minute + 59u) % 60u);
        else edit->minute = (uint8_t)((edit->minute + 1u) % 60u);
        p->dirty = true;
        return;
    }
    if (id >= IW_ACTION_ALARM_WEEKDAY_BASE && id < IW_ACTION_ALARM_WEEKDAY_BASE + 7u) {
        p->model.alarm_edit.weekday_mask ^= (uint8_t)(1u << (id - IW_ACTION_ALARM_WEEKDAY_BASE));
        p->dirty = true;
        return;
    }
    if (id == IW_ACTION_ALARM_ENABLE) {
        p->model.alarm_edit.enabled ^= 1u;
        p->dirty = true;
        return;
    }
    if (id == IW_ACTION_ALARM_SAVE) { submit_alarm_apply(p); return; }
    if (id == IW_ACTION_ALARM_DELETE) { submit_alarm_delete(p); return; }
    if (id == IW_ACTION_ALERT_ACK || id == IW_ACTION_ALERT_SNOOZE) {
        submit_alert_control(p, id == IW_ACTION_ALERT_ACK ?
                             IW_OPCODE_ACK_ALERT : IW_OPCODE_SNOOZE_ALERT);
        return;
    }
    if (id == IW_ACTION_ALERT_OPEN) {
        if (!p->model.selected_alert.entity_id) {
            p->model.message = IW_TEXT_NOTIFICATION_CHANGED;
            p->dirty = true;
            return;
        }
        iw_alert_record_t expected = p->model.selected_alert;
        refresh_action_snapshots(p);
        if (alert_same(&expected, &p->model.selected_alert)) {
            const iw_alert_record_t *record = &p->model.selected_alert;
            p->navigate(record->source_type == IW_ALERT_SOURCE_TIMER ?
                        IW_PAGE_ALERT_TIMER : IW_PAGE_ALERT_ALARM,
                        record->entity_id, p->context);
        } else {
            p->model.message = expected.source_type == IW_ALERT_SOURCE_TIMER ?
                               IW_TEXT_TIMER_CHANGED : IW_TEXT_ALARM_CHANGED;
            p->dirty = true;
        }
        return;
    }
    if (id >= IW_ACTION_NOTIFICATION_OPEN_BASE &&
        id < IW_ACTION_NOTIFICATION_OPEN_BASE + IW_NOTIFICATION_CAPACITY) {
        uint32_t notification_id = p->model.notification_ids[id - IW_ACTION_NOTIFICATION_OPEN_BASE];
        if (iw_notification_find(&notification_store, notification_id))
            p->navigate(IW_PAGE_NOTIFICATION_DETAIL, notification_id, p->context);
        else {
            p->model.message = IW_TEXT_NOTIFICATION_CHANGED;
            p->dirty = true;
        }
        return;
    }
    if (id == IW_ACTION_NOTIFICATION_DELETE) {
        iw_notification_t *selected = &p->model.selected_notification;
        if (!p->model.selected_notification_valid ||
            iw_notification_delete(&notification_store, selected->id,
                                   selected->revision) != IW_NOTIFICATION_OK) {
            p->model.message = IW_TEXT_NOTIFICATION_CHANGED;
            p->dirty = true;
        } else {
            refresh_notification_pages();
            p->navigate(IW_ACTION_BACK, 0u, p->context);
        }
        return;
    }
    if (id == IW_ACTION_STACK_TIMER) {
        uint32_t expected_id = p->model.stack_timer_id;
        refresh_action_snapshots(p);
        /* 刷新后继续绑定原卡片；不能因为原实体失效而换成另一个运行中的计时器。 */
        p->model.stack_timer_id = expected_id;
        const iw_timer_view_t *timer = find_timer(&p->model.timers, expected_id);
        if (timer && timer->state == IW_TIMER_RUNNING) {
            p->navigate(IW_PAGE_TIMER_DETAIL, expected_id, p->context);
            return;
        }
        p->model.message = IW_TEXT_TIMER_CHANGED;
        p->dirty = true;
        return;
    }
    if (id == IW_ACTION_STACK_ALARM) {
        uint32_t expected_id = p->model.stack_alarm_id;
        refresh_action_snapshots(p);
        /* 同理保留原闹钟 ID，重复点击仍只报告原卡片已变化。 */
        p->model.stack_alarm_id = expected_id;
        const iw_alarm_t *alarm = find_alarm(&p->model.alarms, expected_id);
        if (alarm && alarm->enabled && alarm->next_due_utc_ms) {
            p->navigate(IW_PAGE_ALARM_LIST, 0u, p->context);
            return;
        }
        p->model.message = IW_TEXT_ALARM_CHANGED;
        p->dirty = true;
        return;
    }
    if (p->page_id == IW_PAGE_SWITCHER &&
        (id == IW_ACTION_RECENT_PREVIOUS || id == IW_ACTION_RECENT_NEXT)) {
        unsigned count = p->model.recent_apps ? p->model.recent_apps->count : 0u;
        if (count > 1u) {
            if (id == IW_ACTION_RECENT_PREVIOUS) {
                if (p->model.recent_index) p->model.recent_index--;
            } else if (p->model.recent_index + 1u < count) {
                p->model.recent_index++;
            }
            p->dirty = true;
        }
        return;
    }
    if (id >= IW_ACTION_TIMER_PRESET_1M && id <= IW_ACTION_TIMER_PRESET_10M) {
        static const uint32_t duration[] = {60000u, 180000u, 300000u, 600000u};
        submit_timer_create(p, duration[id - IW_ACTION_TIMER_PRESET_1M]);
        return;
    } else if (id >= IW_ACTION_TIMER_OPEN_BASE && id < IW_ACTION_TIMER_OPEN_BASE + IW_TIMER_CAPACITY) {
        unsigned index = id - IW_ACTION_TIMER_OPEN_BASE;
        uint32_t expected_id = index < p->model.timers.count ?
                               p->model.timers.timers[index].timer_id : 0u;
        refresh_action_snapshots(p);
        if (expected_id && index < p->model.timers.count &&
            p->model.timers.timers[index].timer_id == expected_id)
            p->navigate(IW_PAGE_TIMER_DETAIL, expected_id, p->context);
        else {
            p->model.message = IW_TEXT_TIMER_CHANGED;
            p->dirty = true;
        }
        return;
    } else if (id == IW_ACTION_TIMER_PAUSE || id == IW_ACTION_TIMER_RESUME ||
               id == IW_ACTION_TIMER_CANCEL || id == IW_ACTION_TIMER_RESTART) {
        iw_opcode_t opcode = id == IW_ACTION_TIMER_PAUSE ? IW_OPCODE_TIMER_PAUSE :
                             id == IW_ACTION_TIMER_RESUME ? IW_OPCODE_TIMER_RESUME :
                             id == IW_ACTION_TIMER_CANCEL ? IW_OPCODE_TIMER_CANCEL : IW_OPCODE_TIMER_RESTART;
        submit_timer_control(p, opcode);
        return;
    } else if (id == IW_ACTION_STOPWATCH_PRIMARY) {
        submit_stopwatch(p, p->model.stopwatch.state == IW_STOPWATCH_RUNNING ?
                            IW_OPCODE_STOPWATCH_PAUSE : IW_OPCODE_STOPWATCH_START);
        return;
    } else if (id == IW_ACTION_STOPWATCH_LAP) {
        submit_stopwatch(p, IW_OPCODE_STOPWATCH_LAP);
        return;
    } else if (id == IW_ACTION_STOPWATCH_RESET) {
        submit_stopwatch(p, IW_OPCODE_STOPWATCH_RESET);
        return;
    } else if (id == IW_ACTION_RELOAD) {
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
        p->navigate(id, 0u, p->context);
        return;
    }
    p->dirty = true;
}

bool iw_product_create(iw_product_page_t *p, uint16_t id, uint32_t argument,
                       uint32_t generation, bool back,
                       void (*navigate)(uint16_t, uint32_t, void *),
                       void (*quiesce)(void *), void *context) {
    if (!p || p->linked || !generation || !navigate || !iw_font_port_is_owner()) return false;
    p->page_id = id;
    p->generation = generation;
    p->argument = argument;
    p->navigate = navigate;
    p->quiesce = quiesce;
    p->context = context;
    p->model = (iw_product_model_t){.message = IW_TEXT_COUNT, .back = back,
        .hardware = "SF32LB58 A128 QSPI", .firmware = IW_BUILD_TAG, .quality = profile_quality,
        .large_text = profile_large, .reduced_motion = profile_reduced,
        .notifications = &notification_store, .face_session = face_session};
    p->model.lock_water = id == IW_PAGE_WATER_LOCK;
    p->model.lock_available = lock_available;
#if defined(__ARMCOMPILER_VERSION)
    p->model.toolchain = "Arm Compiler " __VERSION__;
#elif defined(__GNUC__)
    p->model.toolchain = "GCC " __VERSION__;
#endif
    (void)iw_clock_read(&p->model.clock);
    (void)iw_brightness_read(&p->model.brightness);
    chronographs(p);
    alarms_alerts(p);
    if (id == IW_PAGE_NOTIFICATION_LIST) notification_projection(&p->model);
    if (id == IW_PAGE_NOTIFICATION_DETAIL) {
        const iw_notification_t *selected = iw_notification_find(&notification_store, argument);
        if (!selected) return false;
        p->model.selected_notification = *selected;
        p->model.selected_notification_valid = true;
    }
    capabilities(&p->model);
    if (id == IW_PAGE_TIME && !iw_time_draft_begin(&p->model.draft, &p->model.clock)) return false;
    if (id == IW_PAGE_TIME && p->model.draft.needs_calibration) p->model.message = IW_TEXT_CALIBRATE;
    if (id != IW_PAGE_TIME) p->model.draft.editing = IW_EDIT_NONE;
    if (id == IW_PAGE_FACE_EDITOR) {
        p->model.face_draft.value = face_session;
        p->model.face_draft.expected_revision = face_session.revision;
        p->model.face_draft.valid = argument == IW_FACE_DIGITAL || argument == IW_FACE_MODULAR_LOCAL;
        if (!p->model.face_draft.valid) return false;
        p->model.face_draft.value.active_face_id = (uint16_t)argument;
        if (argument == IW_FACE_DIGITAL) {
            p->model.face_draft.value.center = IW_FACE_CENTER_NONE;
            p->model.face_draft.value.left = IW_FACE_LEFT_SETTINGS;
            p->model.face_draft.value.right = IW_FACE_RIGHT_ABOUT;
        }
    }
    if (id == IW_PAGE_ALARM_EDIT) {
        iw_alarm_edit_t *edit = &p->model.alarm_edit;
        iw_calendar_fields_t local;
        bool found = false;
        edit->hour = iw_clock_local_fields(&p->model.clock, &local) ? local.hour : 7u;
        edit->minute = iw_clock_local_fields(&p->model.clock, &local) ? local.minute : 0u;
        edit->enabled = 1u;
        memcpy(edit->label, "闹钟", sizeof("闹钟"));
        if (argument)
            for (unsigned i = 0; i < p->model.alarms.count; i++)
                if (p->model.alarms.alarms[i].alarm_id == argument) {
                    const iw_alarm_t *alarm = &p->model.alarms.alarms[i];
                    edit->alarm_id = alarm->alarm_id;
                    edit->expected_revision = alarm->revision;
                    edit->hour = alarm->hour;
                    edit->minute = alarm->minute;
                    edit->weekday_mask = alarm->weekday_mask;
                    edit->enabled = alarm->enabled;
                    memcpy(edit->label, alarm->label, IW_ALARM_LABEL_BYTES);
                    found = true;
                    break;
                }
        if (argument && !found) return false;
    }
    p->exiting = false;
    if (!iw_product_view_create(&p->view, lv_screen_active(), id, &p->model, action, stop, p)) return false;
    if (id == IW_PAGE_NOTIFICATION_DETAIL && !p->model.selected_notification.read) {
        iw_notification_t *selected = &p->model.selected_notification;
        if (iw_notification_mark_read(&notification_store, selected->id,
                                      selected->revision) == IW_NOTIFICATION_OK) {
            const iw_notification_t *updated = iw_notification_find(&notification_store, selected->id);
            if (updated) *selected = *updated;
            refresh_notification_pages();
        }
    }
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
    if (p->page_id == IW_PAGE_FACE) p->model.face_session = face_session;
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
                    p->navigate(IW_ACTION_BACK, 0u, p->context);
                if (p->page_id == IW_PAGE_TIMER_DETAIL && result.opcode == IW_OPCODE_TIMER_CANCEL && p->visible)
                    p->navigate(IW_ACTION_BACK, 0u, p->context);
                if (p->page_id == IW_PAGE_ALARM_EDIT && p->visible &&
                    (result.opcode == IW_OPCODE_ALARM_APPLY || result.opcode == IW_OPCODE_ALARM_DELETE))
                    p->navigate(IW_ACTION_BACK, 0u, p->context);
                if ((p->page_id == IW_PAGE_ALERT_TIMER || p->page_id == IW_PAGE_ALERT_ALARM) &&
                    p->visible && (result.opcode == IW_OPCODE_ACK_ALERT ||
                                   result.opcode == IW_OPCODE_SNOOZE_ALERT))
                    p->navigate(IW_ACTION_BACK, 0u, p->context);
            } else if (result.code == IW_RESULT_CAPACITY && p->page_id == IW_PAGE_STOPWATCH)
                p->model.message = IW_TEXT_LAPS_FULL;
            else if (result.code == IW_RESULT_CAPACITY && p->page_id == IW_PAGE_TIMER_LIST)
                p->model.message = IW_TEXT_TIMERS_FULL;
            else if ((result.code == IW_RESULT_STATE_CONFLICT || result.code == IW_RESULT_ABSENT) &&
                     (p->page_id == IW_PAGE_TIMER_LIST || p->page_id == IW_PAGE_TIMER_DETAIL ||
                      p->page_id == IW_PAGE_ALARM_LIST || p->page_id == IW_PAGE_ALARM_EDIT ||
                      p->page_id == IW_PAGE_ALERT_TIMER || p->page_id == IW_PAGE_ALERT_ALARM))
                p->model.message = entity_changed_text(p->page_id);
            else
                p->model.message = result.code == IW_RESULT_STATE_CONFLICT && p->page_id == IW_PAGE_TIME ? IW_TEXT_TIME_CONFLICT
                                   : p->page_id == IW_PAGE_TIME ? IW_TEXT_TIME_FAILED
                                   : p->page_id == IW_PAGE_BRIGHTNESS ? IW_TEXT_BRIGHTNESS_FAILED
                                                                      : IW_TEXT_OPERATION_FAILED;
            chronographs(p);
            alarms_alerts(p);
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
        uint32_t poll_ms = p->page_id == IW_PAGE_STOPWATCH ? 100u :
                           (p->page_id == IW_PAGE_TIMER_LIST || p->page_id == IW_PAGE_TIMER_DETAIL ||
                            p->page_id == IW_PAGE_SMART_STACK) ? 250u : 1000u;
        if ((uint32_t)(now - p->last_poll) >= poll_ms) {
            (void)iw_clock_read(&p->model.clock);
            (void)iw_brightness_read(&p->model.brightness);
            chronographs(p);
            alarms_alerts(p);
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
