#include "iw_service.h"

#include <limits.h>
#include <string.h>

#define IW_SNAPSHOT_VERSION 1u
#define IW_SNAPSHOT_FLAG_VALID 1u

typedef struct
{
    iw_snapshot_header_t header;
    iw_clock_snapshot_t payload;
} iw_clock_snapshot_message_t;

typedef struct
{
    iw_snapshot_header_t header;
    iw_capability_snapshot_t payload;
} iw_capability_snapshot_message_t;

typedef struct
{
    iw_snapshot_header_t header;
    iw_service_stats_t payload;
} iw_service_snapshot_message_t;

typedef struct
{
    iw_snapshot_header_t header;
    iw_brightness_snapshot_t payload;
} iw_brightness_snapshot_message_t;

typedef struct
{
    iw_snapshot_header_t header;
    iw_timer_snapshot_t payload;
} iw_timer_snapshot_message_t;

typedef struct
{
    iw_snapshot_header_t header;
    iw_alarm_snapshot_t payload;
} iw_alarm_snapshot_message_t;

typedef struct
{
    iw_snapshot_header_t header;
    iw_alert_snapshot_t payload;
} iw_alert_snapshot_message_t;

typedef char iw_stopwatch_prefix_layout_must_match[
    sizeof(iw_stopwatch_summary_t) == offsetof(iw_stopwatch_snapshot_t, laps) ? 1 : -1];

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void count_add(uint32_t *value)
{
    if (*value != UINT32_MAX) (*value)++;
}

static void service_changed(iw_service_t *service)
{
    if (service->stats.service_revision != UINT32_MAX) service->stats.service_revision++;
}

static void refresh_alert_output(iw_service_t *service, uint64_t mono_ms);

static bool brightness_equal_without_revision(const iw_brightness_snapshot_t *left,
                                              const iw_brightness_snapshot_t *right)
{
    return left->desired_revision == right->desired_revision &&
           left->applied_revision == right->applied_revision &&
           left->persisted_revision == right->persisted_revision &&
           left->setting_sequence == right->setting_sequence &&
           left->applied_sequence == right->applied_sequence &&
           left->last_error == right->last_error &&
           left->desired == right->desired && left->applied == right->applied &&
           left->persisted == right->persisted && left->flags == right->flags;
}

/* 先验证 revision 容量，再一次性提交新快照，保证失败时没有部分写入。 */
static bool brightness_commit(iw_service_t *service,
                              const iw_brightness_snapshot_t *candidate,
                              bool *changed)
{
    iw_brightness_snapshot_t next;

    if (!service || !candidate || !changed) return false;
    *changed = !brightness_equal_without_revision(&service->brightness, candidate);
    if (!*changed) return true;
    if (service->brightness.revision == UINT32_MAX) return false;
    next = *candidate;
    next.revision = service->brightness.revision + 1u;
    service->brightness = next;
    return true;
}

static bool commands_equal(const iw_command_t *left, const iw_command_t *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static int find_slot_by_request(const iw_service_t *service, uint32_t request_id)
{
    for (unsigned i = 0; i < IW_RESULT_LEDGER_CAPACITY; i++)
    {
        if (service->slots[i].used && service->slots[i].result.request_id == request_id)
            return (int)i;
    }
    return -1;
}

static int find_free_slot(const iw_service_t *service)
{
    for (unsigned i = 0; i < IW_RESULT_LEDGER_CAPACITY; i++)
    {
        if (!service->slots[i].used) return (int)i;
    }
    return -1;
}

static unsigned ledger_used(const iw_service_t *service)
{
    unsigned used = 0;
    for (unsigned i = 0; i < IW_RESULT_LEDGER_CAPACITY; i++)
    {
        if (service->slots[i].used) used++;
    }
    return used;
}

static iw_service_slot_t *slot_from_token(iw_service_t *service, const iw_result_token_t *token)
{
    int index;

    if (!service || !token || token->session_id != service->session_id) return NULL;
    index = find_slot_by_request(service, token->request_id);
    if (index < 0 || service->slots[index].generation != token->ledger_generation) return NULL;
    return &service->slots[index];
}

static iw_alarm_draft_slot_t *find_alarm_draft(iw_service_t *service, uint32_t handle)
{
    if (!handle) return NULL;
    for (unsigned i = 0; i < IW_ALARM_DRAFT_CAPACITY; i++)
        if (service->alarm_drafts[i].handle == handle) return &service->alarm_drafts[i];
    return NULL;
}

static void result_payload_set_clock(iw_result_t *result, const iw_clock_snapshot_t *clock)
{
    uint32_t utc_seconds = clock->valid ? (uint32_t)(clock->utc_ms / 1000) : 0u;

    memset(result->payload, 0, sizeof(result->payload));
    write_u32(&result->payload[0], utc_seconds);
    write_u32(&result->payload[4], clock->revision);
    write_u16(&result->payload[8], (uint16_t)clock->offset_minutes);
    result->payload[10] = clock->valid;
    result->payload[11] = clock->source;
}

static void result_payload_set_brightness(iw_result_t *result,
                                          const iw_brightness_snapshot_t *brightness)
{
    memset(result->payload, 0, sizeof(result->payload));
    result->payload[0] = brightness->desired;
    result->payload[1] = brightness->applied;
    result->payload[2] = brightness->persisted;
    result->payload[3] = brightness->flags;
    write_u32(&result->payload[4], brightness->desired_revision);
    write_u32(&result->payload[8], brightness->applied_revision);
}

static void result_payload_set_timer(iw_result_t *result, const iw_timer_view_t *timer)
{
    memset(result->payload, 0, sizeof(result->payload));
    if (!timer) return;
    write_u32(&result->payload[0], timer->timer_id);
    write_u32(&result->payload[4], timer->revision);
    write_u32(&result->payload[8], timer->occurrence);
    result->model_revision = timer->revision;
}

static void result_payload_set_stopwatch(iw_result_t *result,
                                         const iw_stopwatch_t *stopwatch)
{
    memset(result->payload, 0, sizeof(result->payload));
    if (!stopwatch) return;
    write_u32(&result->payload[0], stopwatch->revision);
    write_u16(&result->payload[4], stopwatch->lap_count);
    result->payload[6] = stopwatch->state;
    result->model_revision = stopwatch->revision;
}

static iw_result_code_t chrono_result(iw_chrono_status_t status)
{
    switch (status)
    {
    case IW_CHRONO_OK: return IW_RESULT_OK_APPLIED;
    case IW_CHRONO_ABSENT: return IW_RESULT_ABSENT;
    case IW_CHRONO_CONFLICT: return IW_RESULT_STATE_CONFLICT;
    case IW_CHRONO_CAPACITY: return IW_RESULT_CAPACITY;
    default: return IW_RESULT_INVALID;
    }
}

static iw_result_code_t alarm_result(iw_alarm_status_t status)
{
    switch (status)
    {
    case IW_ALARM_OK: return IW_RESULT_OK_APPLIED;
    case IW_ALARM_ABSENT: return IW_RESULT_ABSENT;
    case IW_ALARM_CONFLICT: return IW_RESULT_STATE_CONFLICT;
    case IW_ALARM_NO_CAPACITY: return IW_RESULT_CAPACITY;
    default: return IW_RESULT_INVALID;
    }
}

static iw_result_code_t alert_result(iw_alert_status_t status)
{
    switch (status)
    {
    case IW_ALERT_OK: case IW_ALERT_UNCHANGED: return IW_RESULT_OK_APPLIED;
    case IW_ALERT_ABSENT: return IW_RESULT_ABSENT;
    case IW_ALERT_CONFLICT: return IW_RESULT_STATE_CONFLICT;
    case IW_ALERT_NO_CAPACITY: return IW_RESULT_CAPACITY;
    default: return IW_RESULT_INVALID;
    }
}

static void finish_slot(iw_service_t *service,
                        iw_service_slot_t *slot,
                        iw_result_code_t code,
                        const iw_clock_snapshot_t *clock)
{
    slot->result.state = IW_RESULT_STATE_TERMINAL;
    slot->result.code = (uint8_t)code;
    if (clock)
    {
        slot->result.completed_mono_ms = clock->mono_ms;
        if (slot->command.opcode == IW_OPCODE_SET_CLOCK)
        {
            slot->result.model_revision = clock->revision;
            result_payload_set_clock(&slot->result, clock);
        }
    }
    if (slot->command.opcode == IW_OPCODE_SET_BRIGHTNESS)
    {
        if (clock) slot->result.completed_mono_ms = clock->mono_ms;
        result_payload_set_brightness(&slot->result, &service->brightness);
    }
    if (service->active_count) service->active_count--;
    count_add(&service->stats.completed);
    service_changed(service);
}

static bool command_shape_valid(const iw_command_t *command)
{
    iw_set_clock_payload_t payload;
    iw_set_brightness_payload_t brightness = {0};
    iw_timer_create_payload_t timer_create;
    iw_timer_control_payload_t timer_control;
    iw_stopwatch_control_payload_t stopwatch_control;
    iw_alert_control_payload_t alert_control;
    iw_alarm_apply_payload_t alarm_apply;
    iw_alarm_delete_payload_t alarm_delete;

    if (!command || command->version != IW_COMMAND_VERSION || command->request_id == 0u)
        return false;
    if (command->opcode == IW_OPCODE_SET_CLOCK)
    {
        if (!iw_command_decode_set_clock(command, &payload) || payload.flags != 0u)
            return false;
        return iw_time_utc_seconds_valid(payload.utc_seconds) &&
               iw_time_offset_valid(payload.offset_minutes) && payload.expected_revision != 0u;
    }
    if (command->opcode == IW_OPCODE_SET_BRIGHTNESS)
    {
        if (!iw_command_decode_set_brightness(command, &brightness)) return false;
        return brightness.level >= IW_BRIGHTNESS_MIN && brightness.level <= IW_BRIGHTNESS_MAX &&
               (brightness.kind == IW_BRIGHTNESS_PREVIEW ||
                brightness.kind == IW_BRIGHTNESS_FINAL) &&
               brightness.reserved == 0u && brightness.expected_revision != 0u &&
               brightness.setting_sequence != 0u;
    }
    if (command->opcode == IW_OPCODE_TIMER_CREATE)
        return iw_command_decode_timer_create(command, &timer_create) &&
               timer_create.duration_ms >= IW_TIMER_MIN_DURATION_MS &&
               timer_create.duration_ms <= IW_TIMER_MAX_DURATION_MS;
    if (command->opcode >= IW_OPCODE_TIMER_PAUSE && command->opcode <= IW_OPCODE_TIMER_RESTART)
        return iw_command_decode_timer_control(command, &timer_control) &&
               timer_control.timer_id != 0u && timer_control.expected_revision != 0u;
    if (command->opcode >= IW_OPCODE_STOPWATCH_START &&
            command->opcode <= IW_OPCODE_STOPWATCH_LAP)
        return iw_command_decode_stopwatch_control(command, &stopwatch_control) &&
               stopwatch_control.expected_revision != 0u;
    if (command->opcode == IW_OPCODE_ACK_ALERT || command->opcode == IW_OPCODE_SNOOZE_ALERT)
        return iw_command_decode_alert_control(command, &alert_control) &&
               alert_control.entity_id != 0u && alert_control.occurrence != 0u &&
               (alert_control.source_type == IW_ALERT_SOURCE_TIMER ||
                alert_control.source_type == IW_ALERT_SOURCE_ALARM) &&
               alert_control.reserved == 0u;
    if (command->opcode == IW_OPCODE_ALARM_APPLY)
        return iw_command_decode_alarm_apply(command, &alarm_apply) &&
               alarm_apply.edit_handle != 0u;
    if (command->opcode == IW_OPCODE_ALARM_DELETE)
        return iw_command_decode_alarm_delete(command, &alarm_delete) &&
               alarm_delete.alarm_id != 0u && alarm_delete.expected_revision != 0u;
    return false;
}

static bool command_opcode_supported(uint16_t opcode)
{
    return opcode == IW_OPCODE_SET_CLOCK || opcode == IW_OPCODE_SET_BRIGHTNESS ||
           (opcode >= IW_OPCODE_TIMER_CREATE && opcode <= IW_OPCODE_TIMER_RESTART) ||
           (opcode >= IW_OPCODE_ACK_ALERT && opcode <= IW_OPCODE_SNOOZE_ALERT) ||
           (opcode >= IW_OPCODE_ALARM_APPLY && opcode <= IW_OPCODE_ALARM_DELETE) ||
           (opcode >= IW_OPCODE_STOPWATCH_START && opcode <= IW_OPCODE_STOPWATCH_LAP);
}

void iw_command_init(iw_command_t *command,
                     uint32_t session_id,
                     uint32_t request_id,
                     uint16_t page_id,
                     uint32_t page_generation,
                     iw_opcode_t opcode)
{
    if (!command) return;
    memset(command, 0, sizeof(*command));
    command->session_id = session_id;
    command->request_id = request_id;
    command->page_id = page_id;
    command->page_generation = page_generation;
    command->opcode = (uint16_t)opcode;
    command->version = IW_COMMAND_VERSION;
}

bool iw_command_encode_set_clock(iw_command_t *command,
                                 uint32_t utc_seconds,
                                 int16_t offset_minutes,
                                 uint32_t expected_revision)
{
    if (!command || command->opcode != IW_OPCODE_SET_CLOCK ||
            !iw_time_utc_seconds_valid(utc_seconds) ||
            !iw_time_offset_valid(offset_minutes) || expected_revision == 0u)
        return false;

    memset(command->payload, 0, sizeof(command->payload));
    write_u32(&command->payload[0], utc_seconds);
    write_u16(&command->payload[4], (uint16_t)offset_minutes);
    write_u16(&command->payload[6], 0u);
    write_u32(&command->payload[8], expected_revision);
    command->payload_bytes = 12u;
    return true;
}

bool iw_command_decode_set_clock(const iw_command_t *command, iw_set_clock_payload_t *payload)
{
    if (!command || !payload || command->opcode != IW_OPCODE_SET_CLOCK ||
            command->payload_bytes != 12u)
        return false;
    payload->utc_seconds = read_u32(&command->payload[0]);
    payload->offset_minutes = (int16_t)read_u16(&command->payload[4]);
    payload->flags = read_u16(&command->payload[6]);
    payload->expected_revision = read_u32(&command->payload[8]);
    return true;
}

bool iw_command_encode_set_brightness(iw_command_t *command,
                                      uint8_t level,
                                      iw_brightness_kind_t kind,
                                      uint32_t expected_revision,
                                      uint32_t setting_sequence)
{
    if (!command || command->opcode != IW_OPCODE_SET_BRIGHTNESS ||
            level < IW_BRIGHTNESS_MIN || level > IW_BRIGHTNESS_MAX ||
            (kind != IW_BRIGHTNESS_PREVIEW && kind != IW_BRIGHTNESS_FINAL) ||
            expected_revision == 0u || setting_sequence == 0u)
        return false;

    memset(command->payload, 0, sizeof(command->payload));
    command->payload[0] = level;
    command->payload[1] = (uint8_t)kind;
    write_u16(&command->payload[2], 0u);
    write_u32(&command->payload[4], expected_revision);
    write_u32(&command->payload[8], setting_sequence);
    command->payload_bytes = 12u;
    return true;
}

bool iw_command_decode_set_brightness(const iw_command_t *command,
                                      iw_set_brightness_payload_t *payload)
{
    if (!command || !payload || command->opcode != IW_OPCODE_SET_BRIGHTNESS ||
            command->payload_bytes != 12u)
        return false;
    payload->level = command->payload[0];
    payload->kind = command->payload[1];
    payload->reserved = read_u16(&command->payload[2]);
    payload->expected_revision = read_u32(&command->payload[4]);
    payload->setting_sequence = read_u32(&command->payload[8]);
    return true;
}

bool iw_command_encode_timer_create(iw_command_t *command, uint32_t duration_ms)
{
    if (!command || command->opcode != IW_OPCODE_TIMER_CREATE ||
            duration_ms < IW_TIMER_MIN_DURATION_MS || duration_ms > IW_TIMER_MAX_DURATION_MS)
        return false;
    memset(command->payload, 0, sizeof(command->payload));
    write_u32(command->payload, duration_ms);
    command->payload_bytes = 4u;
    return true;
}

bool iw_command_decode_timer_create(const iw_command_t *command,
                                    iw_timer_create_payload_t *payload)
{
    if (!command || !payload || command->opcode != IW_OPCODE_TIMER_CREATE ||
            command->payload_bytes != 4u)
        return false;
    payload->duration_ms = read_u32(command->payload);
    return true;
}

bool iw_command_encode_timer_control(iw_command_t *command,
                                     uint32_t timer_id,
                                     uint32_t expected_revision)
{
    if (!command || command->opcode < IW_OPCODE_TIMER_PAUSE ||
            command->opcode > IW_OPCODE_TIMER_RESTART || timer_id == 0u ||
            expected_revision == 0u)
        return false;
    memset(command->payload, 0, sizeof(command->payload));
    write_u32(&command->payload[0], timer_id);
    write_u32(&command->payload[4], expected_revision);
    command->payload_bytes = 8u;
    return true;
}

bool iw_command_decode_timer_control(const iw_command_t *command,
                                     iw_timer_control_payload_t *payload)
{
    if (!command || !payload || command->opcode < IW_OPCODE_TIMER_PAUSE ||
            command->opcode > IW_OPCODE_TIMER_RESTART || command->payload_bytes != 8u)
        return false;
    payload->timer_id = read_u32(&command->payload[0]);
    payload->expected_revision = read_u32(&command->payload[4]);
    return true;
}

bool iw_command_encode_stopwatch_control(iw_command_t *command,
                                         uint32_t expected_revision)
{
    if (!command || command->opcode < IW_OPCODE_STOPWATCH_START ||
            command->opcode > IW_OPCODE_STOPWATCH_LAP || expected_revision == 0u)
        return false;
    memset(command->payload, 0, sizeof(command->payload));
    write_u32(command->payload, expected_revision);
    command->payload_bytes = 4u;
    return true;
}

bool iw_command_decode_stopwatch_control(const iw_command_t *command,
                                         iw_stopwatch_control_payload_t *payload)
{
    if (!command || !payload || command->opcode < IW_OPCODE_STOPWATCH_START ||
            command->opcode > IW_OPCODE_STOPWATCH_LAP || command->payload_bytes != 4u)
        return false;
    payload->expected_revision = read_u32(command->payload);
    return true;
}

bool iw_command_encode_alert_control(iw_command_t *command, iw_alert_source_t source,
                                     uint32_t entity_id, uint32_t occurrence)
{
    if (!command || (command->opcode != IW_OPCODE_ACK_ALERT &&
                     command->opcode != IW_OPCODE_SNOOZE_ALERT) ||
        (source != IW_ALERT_SOURCE_TIMER && source != IW_ALERT_SOURCE_ALARM) ||
        !entity_id || !occurrence) return false;
    memset(command->payload, 0, sizeof(command->payload));
    write_u32(&command->payload[0], entity_id);
    write_u32(&command->payload[4], occurrence);
    write_u16(&command->payload[8], (uint16_t)source);
    command->payload_bytes = 12u;
    return true;
}

bool iw_command_decode_alert_control(const iw_command_t *command,
                                     iw_alert_control_payload_t *payload)
{
    if (!command || !payload || (command->opcode != IW_OPCODE_ACK_ALERT &&
                                  command->opcode != IW_OPCODE_SNOOZE_ALERT) ||
        command->payload_bytes != 12u) return false;
    payload->entity_id = read_u32(&command->payload[0]);
    payload->occurrence = read_u32(&command->payload[4]);
    payload->source_type = read_u16(&command->payload[8]);
    payload->reserved = read_u16(&command->payload[10]);
    return true;
}

bool iw_command_encode_alarm_apply(iw_command_t *command, uint32_t edit_handle,
                                   uint32_t expected_revision)
{
    if (!command || command->opcode != IW_OPCODE_ALARM_APPLY || !edit_handle) return false;
    memset(command->payload, 0, sizeof(command->payload));
    write_u32(&command->payload[0], edit_handle);
    write_u32(&command->payload[4], expected_revision);
    command->payload_bytes = 8u;
    return true;
}

bool iw_command_decode_alarm_apply(const iw_command_t *command,
                                   iw_alarm_apply_payload_t *payload)
{
    if (!command || !payload || command->opcode != IW_OPCODE_ALARM_APPLY ||
        command->payload_bytes != 8u) return false;
    payload->edit_handle = read_u32(&command->payload[0]);
    payload->expected_revision = read_u32(&command->payload[4]);
    return true;
}

bool iw_command_encode_alarm_delete(iw_command_t *command, uint32_t alarm_id,
                                    uint32_t expected_revision)
{
    if (!command || command->opcode != IW_OPCODE_ALARM_DELETE ||
        !alarm_id || !expected_revision) return false;
    memset(command->payload, 0, sizeof(command->payload));
    write_u32(&command->payload[0], alarm_id);
    write_u32(&command->payload[4], expected_revision);
    command->payload_bytes = 8u;
    return true;
}

bool iw_command_decode_alarm_delete(const iw_command_t *command,
                                    iw_alarm_delete_payload_t *payload)
{
    if (!command || !payload || command->opcode != IW_OPCODE_ALARM_DELETE ||
        command->payload_bytes != 8u) return false;
    payload->alarm_id = read_u32(&command->payload[0]);
    payload->expected_revision = read_u32(&command->payload[4]);
    return true;
}

bool iw_client_session_init(iw_client_session_t *client, uint32_t session_id)
{
    if (!client || session_id == 0u) return false;
    client->session_id = session_id;
    client->next_request_id = 1u;
    return true;
}

bool iw_client_next_request(iw_client_session_t *client, uint32_t *request_id)
{
    if (!client || !request_id || client->session_id == 0u || client->next_request_id == 0u)
        return false;
    *request_id = client->next_request_id;
    if (client->next_request_id == UINT32_MAX)
        client->next_request_id = 0u;
    else
        client->next_request_id++;
    return true;
}

bool iw_service_init(iw_service_t *service, iw_time_state_t *time_state, uint32_t session_id)
{
    if (!service || !time_state || session_id == 0u) return false;
    memset(service, 0, sizeof(*service));
    service->time_state = time_state;
    service->session_id = session_id;
    service->next_slot_generation = 1u;
    service->next_alarm_draft_generation = 1u;
    service->capability_revision = 1u;
    service->stats.service_revision = 1u;
    service->brightness.revision = 1u;
    service->brightness.desired_revision = 1u;
    service->brightness.desired = IW_BRIGHTNESS_DEFAULT;
    service->brightness.flags = IW_BRIGHTNESS_FLAG_DESIRED_VALID |
                                IW_BRIGHTNESS_FLAG_SESSION_ONLY;
    iw_alert_output_init(&service->alert_output);
    return iw_chronograph_init(&service->chronograph) &&
           iw_alarms_init(&service->alarms) && iw_alerts_init(&service->alerts);
}

bool iw_service_alarm_draft_store(iw_service_t *service, const iw_alarm_edit_t *edit,
                                  uint32_t *handle)
{
    iw_alarm_draft_slot_t *slot = NULL;
    uint32_t generation;
    if (!service || !edit || !handle || edit->hour > 23u || edit->minute > 59u ||
        edit->weekday_mask > 0x7fu || edit->enabled > 1u ||
        !memchr(edit->label, '\0', IW_ALARM_LABEL_BYTES) ||
        (!edit->alarm_id && edit->expected_revision) ||
        (edit->alarm_id && !edit->expected_revision) ||
        service->next_alarm_draft_generation > (UINT32_MAX - 2u) / 4u)
        return false;
    for (unsigned i = 0; i < IW_ALARM_DRAFT_CAPACITY; i++)
        if (!service->alarm_drafts[i].handle) {
            slot = &service->alarm_drafts[i];
            break;
        }
    if (!slot) return false;
    generation = service->next_alarm_draft_generation++;
    slot->edit = *edit;
    slot->handle = generation * 4u + (uint32_t)(slot - service->alarm_drafts) + 1u;
    slot->request_id = 0u;
    *handle = slot->handle;
    return true;
}

bool iw_service_alarm_draft_discard(iw_service_t *service, uint32_t handle)
{
    iw_alarm_draft_slot_t *slot;
    if (!service) return false;
    slot = find_alarm_draft(service, handle);
    if (!slot || slot->request_id) return false;
    memset(slot, 0, sizeof(*slot));
    return true;
}

uint32_t iw_service_session_id(const iw_service_t *service)
{
    return service ? service->session_id : 0u;
}

bool iw_service_set_capability(iw_service_t *service,
                               iw_capability_id_t capability_id,
                               iw_capability_state_t state,
                               int32_t last_error)
{
    iw_capability_entry_t *entry = NULL;

    if (!service || capability_id == 0 || state > IW_CAP_STATE_FAULT) return false;
    for (unsigned i = 0; i < service->capability_count; i++)
    {
        if (service->capabilities[i].capability_id == (uint16_t)capability_id)
        {
            entry = &service->capabilities[i];
            break;
        }
    }
    if (entry && entry->state == (uint8_t)state && entry->last_error == last_error) return true;
    /* revision 耗尽时必须在分配新条目前失败，保证失败路径没有可见副作用。 */
    if (service->capability_revision == UINT32_MAX) return false;
    if (!entry)
    {
        if (service->capability_count >= IW_CAPABILITY_CAPACITY) return false;
        entry = &service->capabilities[service->capability_count++];
        memset(entry, 0, sizeof(*entry));
        entry->capability_id = (uint16_t)capability_id;
    }
    service->capability_revision++;
    entry->revision = service->capability_revision;
    entry->state = (uint8_t)state;
    entry->last_error = last_error;
    service_changed(service);
    return true;
}

static iw_capability_state_t capability_state(const iw_service_t *service,
                                              iw_capability_id_t capability_id)
{
    for (unsigned i = 0; i < service->capability_count; i++)
    {
        if (service->capabilities[i].capability_id == (uint16_t)capability_id)
            return (iw_capability_state_t)service->capabilities[i].state;
    }
    return IW_CAP_STATE_UNKNOWN;
}

static void supersede_queued_brightness(iw_service_t *service, uint32_t setting_sequence)
{
    uint8_t retained[IW_COMMAND_CAPACITY];
    uint8_t retained_count = 0u;
    iw_clock_snapshot_t clock;
    bool have_clock = iw_time_read(service->time_state, &clock);

    for (unsigned offset = 0; offset < service->queue_count; offset++)
    {
        uint8_t index = service->queue[(service->queue_head + offset) % IW_COMMAND_CAPACITY];
        iw_service_slot_t *queued = index < IW_RESULT_LEDGER_CAPACITY ?
                                    &service->slots[index] : NULL;
        iw_set_brightness_payload_t payload;

        if (queued && queued->used && queued->result.state == IW_RESULT_STATE_QUEUED &&
                queued->result.code == IW_RESULT_PENDING &&
                iw_command_decode_set_brightness(&queued->command, &payload) &&
                payload.setting_sequence < setting_sequence)
        {
            finish_slot(service, queued, IW_RESULT_SUPERSEDED, have_clock ? &clock : NULL);
            continue;
        }
        retained[retained_count++] = index;
    }

    memset(service->queue, 0, sizeof(service->queue));
    memcpy(service->queue, retained, retained_count);
    service->queue_head = 0u;
    service->queue_count = retained_count;
}

iw_submit_status_t iw_service_command_submit(iw_service_t *service, const iw_command_t *command)
{
    int index;
    iw_service_slot_t *slot;
    iw_set_brightness_payload_t brightness = {0};
    iw_alarm_apply_payload_t alarm_apply = {0};
    iw_alarm_draft_slot_t *alarm_draft = NULL;
    iw_result_code_t deferred_code = IW_RESULT_PENDING;
    bool brightness_admitted = false;

    if (!service || !command || !service->time_state) return IW_SUBMIT_INVALID;
    if (command->session_id != service->session_id) return IW_SUBMIT_SESSION_CHANGED;
    if (!command_shape_valid(command))
    {
        count_add(&service->stats.rejected);
        return command_opcode_supported(command->opcode) ? IW_SUBMIT_INVALID :
               IW_SUBMIT_CAPABILITY_UNAVAILABLE;
    }

    index = find_slot_by_request(service, command->request_id);
    if (index >= 0)
    {
        if (!commands_equal(&service->slots[index].command, command))
            return IW_SUBMIT_TOKEN_CONFLICT;
        count_add(&service->stats.duplicates);
        return IW_SUBMIT_DUPLICATE;
    }
    if (command->request_id <= service->stats.accepted_request_high_water)
        return IW_SUBMIT_RESULT_EXPIRED;
    if (command->opcode == IW_OPCODE_ALARM_APPLY)
    {
        (void)iw_command_decode_alarm_apply(command, &alarm_apply);
        alarm_draft = find_alarm_draft(service, alarm_apply.edit_handle);
        if (!alarm_draft || alarm_draft->request_id ||
            alarm_draft->edit.expected_revision != alarm_apply.expected_revision)
            return IW_SUBMIT_INVALID;
    }
    if (command->opcode == IW_OPCODE_SET_BRIGHTNESS)
    {
        iw_capability_state_t display_state = capability_state(service, IW_CAP_DISPLAY);

        if (display_state != IW_CAP_STATE_AVAILABLE && display_state != IW_CAP_STATE_DEGRADED)
        {
            count_add(&service->stats.rejected);
            return IW_SUBMIT_CAPABILITY_UNAVAILABLE;
        }
        (void)iw_command_decode_set_brightness(command, &brightness);
        if (brightness.expected_revision != service->brightness.desired_revision)
            deferred_code = IW_RESULT_STATE_CONFLICT;
        else if (service->brightness.setting_sequence == UINT32_MAX ||
                  service->brightness.desired_revision == UINT32_MAX ||
                  service->brightness.revision >= UINT32_MAX - 1u)
            deferred_code = IW_RESULT_CAPACITY;
        else if (brightness.setting_sequence <= service->brightness.setting_sequence)
            deferred_code = IW_RESULT_STATE_CONFLICT;
        else
            brightness_admitted = true;
    }
    if (service->queue_count >= IW_COMMAND_CAPACITY || service->active_count >= IW_COMMAND_CAPACITY)
    {
        count_add(&service->stats.rejected);
        return IW_SUBMIT_BUSY_NO_ADMISSION;
    }
    index = find_free_slot(service);
    if (index < 0)
    {
        count_add(&service->stats.rejected);
        return IW_SUBMIT_BUSY_NO_ADMISSION;
    }
    if (service->next_slot_generation == 0u) return IW_SUBMIT_BUSY_NO_ADMISSION;

    slot = &service->slots[index];
    memset(slot, 0, sizeof(*slot));
    slot->used = 1u;
    slot->generation = service->next_slot_generation++;
    slot->command = *command;
    slot->result.session_id = command->session_id;
    slot->result.request_id = command->request_id;
    slot->result.page_id = command->page_id;
    slot->result.page_generation = command->page_generation;
    slot->result.opcode = command->opcode;
    slot->result.version = IW_COMMAND_VERSION;
    slot->result.state = IW_RESULT_STATE_QUEUED;
    slot->result.code = (uint8_t)deferred_code;
    slot->result.ledger_generation = slot->generation;
    if (command->opcode == IW_OPCODE_SET_BRIGHTNESS)
        slot->result.model_revision = service->brightness.desired_revision;
    if (brightness_admitted)
    {
        iw_brightness_snapshot_t next = service->brightness;

        next.revision++;
        next.desired_revision++;
        next.setting_sequence = brightness.setting_sequence;
        next.desired = brightness.level;
        next.last_error = 0;
        next.flags |= IW_BRIGHTNESS_FLAG_DESIRED_VALID |
                      IW_BRIGHTNESS_FLAG_SESSION_ONLY;
        service->brightness = next;
        slot->result.model_revision = service->brightness.desired_revision;
        supersede_queued_brightness(service, brightness.setting_sequence);
    }
    if (alarm_draft) alarm_draft->request_id = command->request_id;
    service->queue[(service->queue_head + service->queue_count) % IW_COMMAND_CAPACITY] = (uint8_t)index;
    service->queue_count++;
    service->active_count++;
    service->stats.accepted_request_high_water = command->request_id;
    count_add(&service->stats.submitted);
    service_changed(service);
    return IW_SUBMIT_QUEUED;
}

iw_take_status_t iw_service_take_next(iw_service_t *service, iw_service_work_t *work)
{
    unsigned index;
    iw_service_slot_t *slot;
    iw_set_clock_payload_t clock_payload;
    iw_set_brightness_payload_t brightness_payload;
    iw_clock_snapshot_t clock;
    iw_chrono_status_t chrono_status;
    iw_timer_create_payload_t timer_create;
    iw_timer_control_payload_t timer_control;
    iw_stopwatch_control_payload_t stopwatch_control;
    iw_timer_view_t timer_view_value = {0};
    iw_alert_control_payload_t alert_control;
    iw_alarm_apply_payload_t alarm_apply;
    iw_alarm_delete_payload_t alarm_delete;
    iw_alarm_draft_slot_t *draft;
    iw_alarm_edit_t alarm_edit;
    iw_alarm_t applied_alarm;
    iw_alarm_status_t alarm_status;
    iw_alert_status_t alert_status;

    if (!service || !work || service->queue_count == 0u) return IW_TAKE_EMPTY;
    index = service->queue[service->queue_head];
    service->queue_head = (uint8_t)((service->queue_head + 1u) % IW_COMMAND_CAPACITY);
    service->queue_count--;
    if (index >= IW_RESULT_LEDGER_CAPACITY || !service->slots[index].used) return IW_TAKE_EMPTY;

    slot = &service->slots[index];
    slot->result.state = IW_RESULT_STATE_IN_PROGRESS;
    service_changed(service);
    if (!iw_time_read(service->time_state, &clock))
    {
        finish_slot(service, slot, IW_RESULT_INVALID, NULL);
        return IW_TAKE_COMPLETED;
    }
    (void)iw_service_advance(service, clock.mono_ms);

    if (slot->result.code != IW_RESULT_PENDING)
    {
        finish_slot(service, slot, (iw_result_code_t)slot->result.code, &clock);
        return IW_TAKE_COMPLETED;
    }

    if (slot->command.opcode == IW_OPCODE_SET_CLOCK)
    {
        if (!iw_command_decode_set_clock(&slot->command, &clock_payload))
        {
            finish_slot(service, slot, IW_RESULT_INVALID, &clock);
            return IW_TAKE_COMPLETED;
        }
        if (clock_payload.expected_revision != clock.revision)
        {
            finish_slot(service, slot, IW_RESULT_STATE_CONFLICT, &clock);
            return IW_TAKE_COMPLETED;
        }
        if (clock.revision == UINT32_MAX)
        {
            finish_slot(service, slot, IW_RESULT_CAPACITY, &clock);
            return IW_TAKE_COMPLETED;
        }
    }
    else if (slot->command.opcode == IW_OPCODE_SET_BRIGHTNESS)
    {
        if (!iw_command_decode_set_brightness(&slot->command, &brightness_payload) ||
                slot->result.model_revision == 0u)
        {
            finish_slot(service, slot, IW_RESULT_INVALID, &clock);
            return IW_TAKE_COMPLETED;
        }
    }
    else if (slot->command.opcode == IW_OPCODE_TIMER_CREATE)
    {
        chrono_status = iw_command_decode_timer_create(&slot->command, &timer_create) ?
                        iw_timer_create(&service->chronograph, clock.mono_ms,
                                        timer_create.duration_ms, &timer_view_value) :
                        IW_CHRONO_INVALID;
        result_payload_set_timer(&slot->result, chrono_status == IW_CHRONO_OK ?
                                 &timer_view_value : NULL);
        finish_slot(service, slot, chrono_result(chrono_status), &clock);
        return IW_TAKE_COMPLETED;
    }
    else if (slot->command.opcode >= IW_OPCODE_TIMER_PAUSE &&
             slot->command.opcode <= IW_OPCODE_TIMER_RESTART)
    {
        chrono_status = IW_CHRONO_INVALID;
        if (iw_command_decode_timer_control(&slot->command, &timer_control))
        {
            if (slot->command.opcode == IW_OPCODE_TIMER_PAUSE)
                chrono_status = iw_timer_pause(&service->chronograph, clock.mono_ms,
                                               timer_control.timer_id,
                                               timer_control.expected_revision,
                                               &timer_view_value);
            else if (slot->command.opcode == IW_OPCODE_TIMER_RESUME)
                chrono_status = iw_timer_resume(&service->chronograph, clock.mono_ms,
                                                timer_control.timer_id,
                                                timer_control.expected_revision,
                                                &timer_view_value);
            else if (slot->command.opcode == IW_OPCODE_TIMER_CANCEL)
                chrono_status = iw_timer_cancel(&service->chronograph, clock.mono_ms,
                                                timer_control.timer_id,
                                                timer_control.expected_revision);
            else if (service->alerts.revision == UINT32_MAX)
                chrono_status = IW_CHRONO_CAPACITY;
            else {
                chrono_status = iw_timer_restart(&service->chronograph, clock.mono_ms,
                                                 timer_control.timer_id,
                                                 timer_control.expected_revision,
                                                 &timer_view_value);
                if (chrono_status == IW_CHRONO_OK)
                    (void)iw_alerts_remove_source(&service->alerts, IW_ALERT_SOURCE_TIMER,
                                                  timer_control.timer_id);
                if (chrono_status == IW_CHRONO_OK)
                    refresh_alert_output(service, clock.mono_ms);
            }
        }
        result_payload_set_timer(&slot->result,
                                 chrono_status == IW_CHRONO_OK &&
                                 slot->command.opcode != IW_OPCODE_TIMER_CANCEL ?
                                 &timer_view_value : NULL);
        finish_slot(service, slot, chrono_result(chrono_status), &clock);
        return IW_TAKE_COMPLETED;
    }
    else if (slot->command.opcode == IW_OPCODE_ALARM_APPLY)
    {
        if (!iw_command_decode_alarm_apply(&slot->command, &alarm_apply) ||
            !(draft = find_alarm_draft(service, alarm_apply.edit_handle)) ||
            draft->request_id != slot->command.request_id) {
            finish_slot(service, slot, IW_RESULT_INVALID, &clock);
            return IW_TAKE_COMPLETED;
        }
        alarm_edit = draft->edit;
        memset(draft, 0, sizeof(*draft));
        alarm_status = iw_alarms_apply(&service->alarms, &service->alerts, &alarm_edit,
                                       &clock, &applied_alarm);
        memset(slot->result.payload, 0, sizeof(slot->result.payload));
        if (alarm_status == IW_ALARM_OK) {
            refresh_alert_output(service, clock.mono_ms);
            write_u32(&slot->result.payload[0], applied_alarm.alarm_id);
            write_u32(&slot->result.payload[4], applied_alarm.revision);
            slot->result.model_revision = service->alarms.revision;
        }
        finish_slot(service, slot, alarm_result(alarm_status), &clock);
        return IW_TAKE_COMPLETED;
    }
    else if (slot->command.opcode == IW_OPCODE_ALARM_DELETE)
    {
        alarm_status = iw_command_decode_alarm_delete(&slot->command, &alarm_delete) ?
                       iw_alarms_delete(&service->alarms, &service->alerts,
                                        alarm_delete.alarm_id,
                                        alarm_delete.expected_revision) : IW_ALARM_INVALID;
        if (alarm_status == IW_ALARM_OK) refresh_alert_output(service, clock.mono_ms);
        slot->result.model_revision = service->alarms.revision;
        finish_slot(service, slot, alarm_result(alarm_status), &clock);
        return IW_TAKE_COMPLETED;
    }
    else if (slot->command.opcode == IW_OPCODE_ACK_ALERT ||
             slot->command.opcode == IW_OPCODE_SNOOZE_ALERT)
    {
        if (!iw_command_decode_alert_control(&slot->command, &alert_control)) {
            finish_slot(service, slot, IW_RESULT_INVALID, &clock);
            return IW_TAKE_COMPLETED;
        }
        if (slot->command.opcode == IW_OPCODE_ACK_ALERT) {
            if (alert_control.source_type == IW_ALERT_SOURCE_TIMER) {
                chrono_status = iw_timer_alert_check(&service->chronograph,
                                                     alert_control.entity_id,
                                                     alert_control.occurrence);
                if (chrono_status != IW_CHRONO_OK) {
                    finish_slot(service, slot, chrono_result(chrono_status), &clock);
                    return IW_TAKE_COMPLETED;
                }
            }
            alert_status = iw_alerts_ack(&service->alerts,
                                         (iw_alert_source_t)alert_control.source_type,
                                         alert_control.entity_id,
                                         alert_control.occurrence);
            if (alert_status == IW_ALERT_OK &&
                alert_control.source_type == IW_ALERT_SOURCE_TIMER)
                (void)iw_timer_alert_ack(&service->chronograph, clock.mono_ms,
                                         alert_control.entity_id,
                                         alert_control.occurrence);
        } else {
            alert_status = iw_alerts_snooze(&service->alerts,
                                            (iw_alert_source_t)alert_control.source_type,
                                            alert_control.entity_id,
                                            alert_control.occurrence,
                                            clock.mono_ms);
        }
        slot->result.model_revision = service->alerts.revision;
        if (alert_status == IW_ALERT_OK) refresh_alert_output(service, clock.mono_ms);
        finish_slot(service, slot, alert_result(alert_status), &clock);
        return IW_TAKE_COMPLETED;
    }
    else if (slot->command.opcode >= IW_OPCODE_STOPWATCH_START &&
             slot->command.opcode <= IW_OPCODE_STOPWATCH_LAP)
    {
        chrono_status = IW_CHRONO_INVALID;
        if (iw_command_decode_stopwatch_control(&slot->command, &stopwatch_control))
        {
            if (slot->command.opcode == IW_OPCODE_STOPWATCH_START)
                chrono_status = iw_stopwatch_start(&service->chronograph, clock.mono_ms,
                                                   stopwatch_control.expected_revision);
            else if (slot->command.opcode == IW_OPCODE_STOPWATCH_PAUSE)
                chrono_status = iw_stopwatch_pause(&service->chronograph, clock.mono_ms,
                                                   stopwatch_control.expected_revision);
            else if (slot->command.opcode == IW_OPCODE_STOPWATCH_RESET)
                chrono_status = iw_stopwatch_reset(&service->chronograph,
                                                   stopwatch_control.expected_revision);
            else
                chrono_status = iw_stopwatch_lap(&service->chronograph, clock.mono_ms,
                                                 stopwatch_control.expected_revision);
        }
        result_payload_set_stopwatch(&slot->result, &service->chronograph.stopwatch);
        finish_slot(service, slot, chrono_result(chrono_status), &clock);
        return IW_TAKE_COMPLETED;
    }
    else
    {
        finish_slot(service, slot, IW_RESULT_INVALID, &clock);
        return IW_TAKE_COMPLETED;
    }

    work->command = slot->command;
    work->token.session_id = service->session_id;
    work->token.request_id = slot->result.request_id;
    work->token.ledger_generation = slot->generation;
    return IW_TAKE_WORK;
}

bool iw_service_finish_set_clock(iw_service_t *service,
                                 const iw_result_token_t *token,
                                 uint32_t raw_tick,
                                 bool device_success)
{
    iw_service_slot_t *slot = slot_from_token(service, token);
    iw_set_clock_payload_t payload;
    iw_clock_snapshot_t before;
    iw_clock_snapshot_t after;
    iw_time_status_t status;

    if (!slot || slot->result.state != IW_RESULT_STATE_IN_PROGRESS ||
            !iw_command_decode_set_clock(&slot->command, &payload) ||
            !iw_time_read(service->time_state, &before))
        return false;
    if (!device_success)
    {
        finish_slot(service, slot, IW_RESULT_DEVICE_FAULT, &before);
        return true;
    }
    if (payload.expected_revision != before.revision)
    {
        finish_slot(service, slot, IW_RESULT_STATE_CONFLICT, &before);
        return true;
    }

    status = iw_time_set_clock(service->time_state, raw_tick, payload.utc_seconds,
                               payload.offset_minutes, IW_TIME_SOURCE_MANUAL);
    if (!iw_time_read(service->time_state, &after)) return false;
    if (status == IW_TIME_OK)
        finish_slot(service, slot, IW_RESULT_OK_APPLIED, &after);
    else if (status == IW_TIME_REVISION_EXHAUSTED)
        finish_slot(service, slot, IW_RESULT_CAPACITY, &after);
    else
        finish_slot(service, slot, IW_RESULT_INVALID, &after);
    return true;
}

bool iw_service_finish_set_brightness(iw_service_t *service,
                                      const iw_result_token_t *token,
                                      uint32_t raw_tick,
                                      iw_result_code_t code,
                                      int32_t device_error)
{
    iw_service_slot_t *slot = slot_from_token(service, token);
    iw_set_brightness_payload_t payload;
    iw_clock_snapshot_t clock;
    iw_brightness_snapshot_t next;
    bool state_changed = false;

    if (!slot || slot->result.state != IW_RESULT_STATE_IN_PROGRESS ||
            !iw_command_decode_set_brightness(&slot->command, &payload))
        return false;
    if (code != IW_RESULT_OK_APPLIED && code != IW_RESULT_SUPERSEDED &&
            code != IW_RESULT_DEVICE_BUSY && code != IW_RESULT_UNCONFIRMED_TIMEOUT &&
            code != IW_RESULT_DEVICE_FAULT)
        return false;

    iw_time_sample(service->time_state, raw_tick);
    if (!iw_time_read(service->time_state, &clock)) return false;

    next = service->brightness;
    if (code == IW_RESULT_OK_APPLIED)
    {
        /* 单显示 owner 保证完成有序；仍拒绝迟到结果倒退 applied 版本。 */
        if (slot->result.model_revision >= next.applied_revision)
        {
            next.applied = payload.level;
            next.applied_revision = slot->result.model_revision;
            next.applied_sequence = payload.setting_sequence;
            next.flags |= IW_BRIGHTNESS_FLAG_APPLIED_VALID;
        }
        if (slot->result.model_revision == next.desired_revision)
            next.last_error = 0;
    }
    else if (code != IW_RESULT_SUPERSEDED &&
             slot->result.model_revision == next.desired_revision)
        next.last_error = device_error;

    if (!brightness_commit(service, &next, &state_changed))
    {
        finish_slot(service, slot, IW_RESULT_CAPACITY, &clock);
        return true;
    }
    if (state_changed) service_changed(service);
    finish_slot(service, slot, code, &clock);
    return true;
}

bool iw_service_note_brightness_applied(iw_service_t *service,
                                        uint8_t level,
                                        uint32_t target_revision,
                                        uint32_t target_sequence,
                                        bool device_success,
                                        int32_t device_error)
{
    iw_brightness_snapshot_t next;
    bool changed = false;

    if (!service || level < IW_BRIGHTNESS_MIN || level > IW_BRIGHTNESS_MAX ||
            target_revision != service->brightness.desired_revision ||
            target_sequence != service->brightness.setting_sequence ||
            level != service->brightness.desired)
        return false;

    next = service->brightness;
    if (device_success)
    {
        if (target_revision >= next.applied_revision)
        {
            next.applied = level;
            next.applied_revision = target_revision;
            next.applied_sequence = target_sequence;
            next.flags |= IW_BRIGHTNESS_FLAG_APPLIED_VALID;
        }
        if (target_revision == next.desired_revision) next.last_error = 0;
    }
    else if (target_revision == next.desired_revision)
        next.last_error = device_error;

    if (!brightness_commit(service, &next, &changed)) return false;
    if (changed) service_changed(service);
    return true;
}

bool iw_service_brightness_read(const iw_service_t *service,
                                iw_brightness_snapshot_t *snapshot)
{
    if (!service || !snapshot) return false;
    *snapshot = service->brightness;
    return true;
}

static void refresh_alert_output(iw_service_t *service, uint64_t mono_ms)
{
    if (iw_alert_output_step(&service->alert_output, &service->alerts,
                             mono_ms, false, false, false))
        service_changed(service);
}

unsigned iw_service_advance(iw_service_t *service, uint64_t mono_ms)
{
    unsigned expired, changed, timer_noted = 0u, alarm_fired = 0u, snoozes = 0u;
    bool output_changed;
    iw_clock_snapshot_t clock;
    if (!service) return 0u;
    expired = iw_chronograph_advance(&service->chronograph, mono_ms);
    changed = expired;
    if (iw_time_read(service->time_state, &clock)) {
        clock.mono_ms = mono_ms;
        for (unsigned i = 0; i < IW_TIMER_CAPACITY; i++) {
            const iw_timer_t *timer = &service->chronograph.timers[i];
            uint64_t due_utc_ms = 0u;
            uint64_t late;
            iw_alert_status_t status;
            if (timer->state != IW_TIMER_EXPIRED || !timer->alert_pending) continue;
            late = mono_ms >= timer->deadline_ms ? mono_ms - timer->deadline_ms : 0u;
            if (clock.valid && clock.utc_ms >= 0 && late <= (uint64_t)clock.utc_ms)
                due_utc_ms = (uint64_t)clock.utc_ms - late;
            status = iw_alerts_note(&service->alerts, IW_ALERT_SOURCE_TIMER,
                                    timer->timer_id, timer->occurrence,
                                    timer->deadline_ms, due_utc_ms);
            if (status == IW_ALERT_OK) {
                timer_noted++;
                changed++;
            }
        }
        alarm_fired = iw_alarms_advance(&service->alarms, &service->alerts, &clock);
        snoozes = iw_alerts_advance(&service->alerts, mono_ms);
        changed += alarm_fired + snoozes;
    }
    /* 开发板没有已验收的马达/扬声器驱动，提醒只启动视觉输出。 */
    output_changed = iw_alert_output_step(&service->alert_output, &service->alerts,
                                          mono_ms, false, false, false);
    if (output_changed) changed++;
    if (changed) service_changed(service);
    return (expired > timer_noted ? expired : timer_noted) + alarm_fired + snoozes;
}

bool iw_service_timers_read(const iw_service_t *service,
                            uint64_t mono_ms,
                            iw_timer_snapshot_t *snapshot)
{
    return service && iw_timer_snapshot_read(&service->chronograph, mono_ms, snapshot);
}

bool iw_service_timer_history_read(const iw_service_t *service,
                                   iw_timer_history_snapshot_t *snapshot)
{
    return service && iw_timer_history_read(&service->chronograph, snapshot);
}

bool iw_service_stopwatch_read(const iw_service_t *service,
                               uint64_t mono_ms,
                               iw_stopwatch_snapshot_t *snapshot)
{
    return service && iw_stopwatch_snapshot_read(&service->chronograph, mono_ms, snapshot);
}

bool iw_service_stopwatch_summary_read(const iw_service_t *service,
                                       uint64_t mono_ms,
                                       iw_stopwatch_summary_t *summary)
{
    return service && iw_stopwatch_summary_read(&service->chronograph, mono_ms, summary);
}

bool iw_service_alarms_read(const iw_service_t *service, iw_alarm_snapshot_t *snapshot)
{
    return service && iw_alarms_snapshot_read(&service->alarms, snapshot);
}

bool iw_service_alerts_read(const iw_service_t *service, iw_alert_snapshot_t *snapshot)
{
    return service && iw_alerts_snapshot_read(&service->alerts, snapshot);
}

iw_result_lookup_t iw_service_result_get(const iw_service_t *service,
                                         uint32_t session_id,
                                         uint32_t request_id,
                                         iw_result_t *result)
{
    int index;

    if (!service || !result || request_id == 0u) return IW_RESULT_LOOKUP_INVALID;
    if (session_id != service->session_id) return IW_RESULT_LOOKUP_SESSION_CHANGED;
    index = find_slot_by_request(service, request_id);
    if (index >= 0)
    {
        *result = service->slots[index].result;
        return IW_RESULT_LOOKUP_FOUND;
    }
    if (request_id <= service->stats.accepted_request_high_water)
        return IW_RESULT_LOOKUP_EXPIRED;
    return IW_RESULT_LOOKUP_NOT_FOUND;
}

iw_ack_status_t iw_service_result_ack(iw_service_t *service, const iw_result_token_t *token)
{
    iw_service_slot_t *slot;

    if (!service || !token) return IW_ACK_INVALID;
    if (token->session_id != service->session_id) return IW_ACK_SESSION_CHANGED;
    slot = slot_from_token(service, token);
    if (!slot)
    {
        int index = find_slot_by_request(service, token->request_id);
        return index >= 0 ? IW_ACK_STALE_TOKEN : IW_ACK_NOT_FOUND;
    }
    if (slot->result.state != IW_RESULT_STATE_TERMINAL) return IW_ACK_NOT_TERMINAL;
    memset(slot, 0, sizeof(*slot));
    count_add(&service->stats.acknowledged);
    service_changed(service);
    return IW_ACK_OK;
}

bool iw_result_matches_page(const iw_result_t *result, uint16_t page_id, uint32_t page_generation)
{
    return result && result->page_id == page_id && result->page_generation == page_generation;
}

static void fill_header(iw_snapshot_header_t *header,
                        const iw_service_t *service,
                        iw_snapshot_topic_t topic,
                        uint16_t payload_bytes,
                        uint32_t revision,
                        uint32_t flags)
{
    memset(header, 0, sizeof(*header));
    header->version = IW_SNAPSHOT_VERSION;
    header->topic = (uint16_t)topic;
    header->header_bytes = (uint16_t)sizeof(*header);
    header->payload_bytes = payload_bytes;
    header->session_id = service->session_id;
    header->revision = revision;
    header->flags = flags;
}

iw_snapshot_status_t iw_service_snapshot_read(const iw_service_t *service,
                                              iw_snapshot_topic_t topic,
                                              void *output,
                                              size_t capacity,
                                              size_t *required)
{
    size_t bytes;

    if (!service || !required) return IW_SNAPSHOT_INVALID;
    switch (topic)
    {
    case IW_SNAPSHOT_CLOCK:
    {
        iw_clock_snapshot_message_t message;
        if (!iw_time_read(service->time_state, &message.payload)) return IW_SNAPSHOT_INVALID;
        bytes = sizeof(message);
        *required = bytes;
        if (!output || capacity < bytes) return IW_SNAPSHOT_TOO_SMALL;
        fill_header(&message.header, service, topic, (uint16_t)sizeof(message.payload),
                    message.payload.revision, message.payload.valid ? IW_SNAPSHOT_FLAG_VALID : 0u);
        memcpy(output, &message, bytes);
        return IW_SNAPSHOT_OK;
    }
    case IW_SNAPSHOT_CAPABILITIES:
    {
        iw_capability_snapshot_message_t message;
        memset(&message, 0, sizeof(message));
        message.payload.count = service->capability_count;
        memcpy(message.payload.entries, service->capabilities,
               service->capability_count * sizeof(service->capabilities[0]));
        bytes = sizeof(message.header) + sizeof(message.payload.count) +
                sizeof(message.payload.reserved) +
                service->capability_count * sizeof(service->capabilities[0]);
        *required = bytes;
        if (!output || capacity < bytes) return IW_SNAPSHOT_TOO_SMALL;
        fill_header(&message.header, service, topic, (uint16_t)(bytes - sizeof(message.header)),
                    service->capability_revision, 0u);
        memcpy(output, &message, bytes);
        return IW_SNAPSHOT_OK;
    }
    case IW_SNAPSHOT_SERVICE:
    {
        iw_service_snapshot_message_t message;
        iw_service_stats_read(service, &message.payload);
        bytes = sizeof(message);
        *required = bytes;
        if (!output || capacity < bytes) return IW_SNAPSHOT_TOO_SMALL;
        fill_header(&message.header, service, topic, (uint16_t)sizeof(message.payload),
                    service->stats.service_revision, 0u);
        memcpy(output, &message, bytes);
        return IW_SNAPSHOT_OK;
    }
    case IW_SNAPSHOT_BRIGHTNESS:
    {
        iw_brightness_snapshot_message_t message;
        message.payload = service->brightness;
        bytes = sizeof(message);
        *required = bytes;
        if (!output || capacity < bytes) return IW_SNAPSHOT_TOO_SMALL;
        fill_header(&message.header, service, topic, (uint16_t)sizeof(message.payload),
                    message.payload.revision,
                    (message.payload.flags & IW_BRIGHTNESS_FLAG_DESIRED_VALID) ?
                    IW_SNAPSHOT_FLAG_VALID : 0u);
        memcpy(output, &message, bytes);
        return IW_SNAPSHOT_OK;
    }
    case IW_SNAPSHOT_TIMERS:
    {
        iw_timer_snapshot_message_t message;
        iw_clock_snapshot_t clock;
        if (!iw_time_read(service->time_state, &clock) ||
                !iw_timer_snapshot_read(&service->chronograph, clock.mono_ms, &message.payload))
            return IW_SNAPSHOT_INVALID;
        bytes = sizeof(message.header) + offsetof(iw_timer_snapshot_t, timers) +
                message.payload.count * sizeof(message.payload.timers[0]);
        *required = bytes;
        if (!output || capacity < bytes) return IW_SNAPSHOT_TOO_SMALL;
        fill_header(&message.header, service, topic, (uint16_t)(bytes - sizeof(message.header)),
                    message.payload.revision, IW_SNAPSHOT_FLAG_VALID);
        memcpy(output, &message, bytes);
        return IW_SNAPSHOT_OK;
    }
    case IW_SNAPSHOT_STOPWATCH:
    {
        iw_snapshot_header_t header;
        iw_stopwatch_summary_t summary;
        iw_clock_snapshot_t clock;
        if (!iw_time_read(service->time_state, &clock) ||
                !iw_stopwatch_summary_read(&service->chronograph, clock.mono_ms, &summary))
            return IW_SNAPSHOT_INVALID;
        bytes = sizeof(header) + sizeof(summary) +
                summary.lap_count * sizeof(iw_stopwatch_lap_t);
        *required = bytes;
        if (!output || capacity < bytes) return IW_SNAPSHOT_TOO_SMALL;
        fill_header(&header, service, topic, (uint16_t)(bytes - sizeof(header)),
                    summary.revision, IW_SNAPSHOT_FLAG_VALID);
        memcpy(output, &header, sizeof(header));
        memcpy((uint8_t *)output + sizeof(header), &summary, sizeof(summary));
        memcpy((uint8_t *)output + sizeof(header) + sizeof(summary),
               service->chronograph.stopwatch.laps,
               summary.lap_count * sizeof(iw_stopwatch_lap_t));
        return IW_SNAPSHOT_OK;
    }
    case IW_SNAPSHOT_ALARMS:
    {
        iw_alarm_snapshot_message_t message;
        if (!iw_alarms_snapshot_read(&service->alarms, &message.payload))
            return IW_SNAPSHOT_INVALID;
        bytes = sizeof(message.header) + offsetof(iw_alarm_snapshot_t, alarms) +
                message.payload.count * sizeof(message.payload.alarms[0]);
        *required = bytes;
        if (!output || capacity < bytes) return IW_SNAPSHOT_TOO_SMALL;
        fill_header(&message.header, service, topic, (uint16_t)(bytes - sizeof(message.header)),
                    message.payload.revision, IW_SNAPSHOT_FLAG_VALID);
        memcpy(output, &message, bytes);
        return IW_SNAPSHOT_OK;
    }
    case IW_SNAPSHOT_ALERTS:
    {
        iw_alert_snapshot_message_t message;
        if (!iw_alerts_snapshot_read(&service->alerts, &message.payload))
            return IW_SNAPSHOT_INVALID;
        bytes = sizeof(message.header) + offsetof(iw_alert_snapshot_t, records) +
                message.payload.count * sizeof(message.payload.records[0]);
        *required = bytes;
        if (!output || capacity < bytes) return IW_SNAPSHOT_TOO_SMALL;
        fill_header(&message.header, service, topic, (uint16_t)(bytes - sizeof(message.header)),
                    message.payload.revision, IW_SNAPSHOT_FLAG_VALID);
        memcpy(output, &message, bytes);
        return IW_SNAPSHOT_OK;
    }
    default:
        *required = 0u;
        return IW_SNAPSHOT_TOPIC_UNKNOWN;
    }
}

void iw_service_stats_read(const iw_service_t *service, iw_service_stats_t *stats)
{
    if (!service || !stats) return;
    *stats = service->stats;
    stats->queue_depth = service->queue_count;
    stats->active_count = service->active_count;
    stats->ledger_used = ledger_used(service);
}
