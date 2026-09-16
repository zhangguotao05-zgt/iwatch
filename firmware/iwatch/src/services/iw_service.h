#ifndef IW_SERVICE_H
#define IW_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iw_time.h"

#define IW_COMMAND_VERSION 1u
#define IW_COMMAND_CAPACITY 16u
#define IW_RESULT_LEDGER_CAPACITY 32u
#define IW_CAPABILITY_CAPACITY 8u
#define IW_RESULT_PAYLOAD_BYTES 12u

typedef enum
{
    IW_OPCODE_SET_CLOCK = 0x0001,
    IW_OPCODE_SET_BRIGHTNESS = 0x0002,
    IW_OPCODE_TIMER_CREATE = 0x0100,
    IW_OPCODE_TIMER_PAUSE = 0x0101,
    IW_OPCODE_TIMER_RESUME = 0x0102,
    IW_OPCODE_TIMER_CANCEL = 0x0103,
    IW_OPCODE_TIMER_RESTART = 0x0104,
    IW_OPCODE_ACK_ALERT = 0x0200,
    IW_OPCODE_SNOOZE_ALERT = 0x0201,
    IW_OPCODE_ALARM_APPLY = 0x0300,
    IW_OPCODE_STOPWATCH_START = 0x0400,
    IW_OPCODE_STOPWATCH_PAUSE = 0x0401,
    IW_OPCODE_STOPWATCH_RESET = 0x0402,
    IW_OPCODE_STOPWATCH_LAP = 0x0403
} iw_opcode_t;

typedef struct
{
    uint32_t session_id;
    uint32_t request_id;
    uint32_t page_generation;
    uint16_t page_id;
    uint16_t opcode;
    uint16_t version;
    uint16_t payload_bytes;
    uint8_t payload[12];
} iw_command_t;

_Static_assert(sizeof(iw_command_t) == 32u, "iw_command_t 必须保持 32 字节");

typedef struct
{
    uint32_t utc_seconds;
    uint32_t expected_revision;
    int16_t offset_minutes;
    uint16_t flags;
} iw_set_clock_payload_t;

typedef enum
{
    IW_SUBMIT_QUEUED = 0,
    IW_SUBMIT_DUPLICATE,
    IW_SUBMIT_INVALID,
    IW_SUBMIT_SESSION_CHANGED,
    IW_SUBMIT_RESULT_EXPIRED,
    IW_SUBMIT_TOKEN_CONFLICT,
    IW_SUBMIT_BUSY_NO_ADMISSION,
    IW_SUBMIT_CAPABILITY_UNAVAILABLE
} iw_submit_status_t;

typedef enum
{
    IW_RESULT_STATE_EMPTY = 0,
    IW_RESULT_STATE_QUEUED,
    IW_RESULT_STATE_IN_PROGRESS,
    IW_RESULT_STATE_TERMINAL
} iw_result_state_t;

typedef enum
{
    IW_RESULT_PENDING = 0,
    IW_RESULT_OK_APPLIED,
    IW_RESULT_OK_PERSISTED,
    IW_RESULT_SUPERSEDED,
    IW_RESULT_INVALID,
    IW_RESULT_STATE_CONFLICT,
    IW_RESULT_BUSY_NO_ADMISSION,
    IW_RESULT_CAPACITY,
    IW_RESULT_ABSENT,
    IW_RESULT_DEVICE_FAULT,
    IW_RESULT_NO_MEMORY,
    IW_RESULT_STORAGE_UNAVAILABLE,
    IW_RESULT_UNCONFIRMED_TIMEOUT,
    IW_RESULT_EXPIRED,
    IW_RESULT_SESSION_CHANGED
} iw_result_code_t;

typedef struct
{
    uint32_t session_id;
    uint32_t request_id;
    uint32_t page_generation;
    uint32_t ledger_generation;
    uint64_t completed_mono_ms;
    uint32_t model_revision;
    uint16_t page_id;
    uint16_t opcode;
    uint16_t version;
    uint8_t state;
    uint8_t code;
    uint8_t payload[IW_RESULT_PAYLOAD_BYTES];
} iw_result_t;

_Static_assert(sizeof(iw_result_t) == 48u, "iw_result_t 必须保持 48 字节");

typedef struct
{
    uint32_t session_id;
    uint32_t request_id;
    uint32_t ledger_generation;
} iw_result_token_t;

typedef enum
{
    IW_RESULT_LOOKUP_FOUND = 0,
    IW_RESULT_LOOKUP_NOT_FOUND,
    IW_RESULT_LOOKUP_EXPIRED,
    IW_RESULT_LOOKUP_SESSION_CHANGED,
    IW_RESULT_LOOKUP_INVALID
} iw_result_lookup_t;

typedef enum
{
    IW_ACK_OK = 0,
    IW_ACK_NOT_TERMINAL,
    IW_ACK_STALE_TOKEN,
    IW_ACK_NOT_FOUND,
    IW_ACK_SESSION_CHANGED,
    IW_ACK_INVALID
} iw_ack_status_t;

typedef enum
{
    IW_TAKE_EMPTY = 0,
    IW_TAKE_WORK,
    IW_TAKE_COMPLETED
} iw_take_status_t;

typedef struct
{
    iw_command_t command;
    iw_result_token_t token;
} iw_service_work_t;

typedef enum
{
    IW_CAP_CLOCK = 1,
    IW_CAP_RTC_BACKUP = 2,
    IW_CAP_DISPLAY = 3,
    IW_CAP_STORAGE = 4
} iw_capability_id_t;

typedef enum
{
    IW_CAP_STATE_UNKNOWN = 0,
    IW_CAP_STATE_AVAILABLE,
    IW_CAP_STATE_DEGRADED,
    IW_CAP_STATE_ABSENT,
    IW_CAP_STATE_FAULT
} iw_capability_state_t;

typedef struct
{
    uint32_t revision;
    int32_t last_error;
    uint16_t capability_id;
    uint8_t state;
    uint8_t reserved;
} iw_capability_entry_t;

_Static_assert(sizeof(iw_capability_entry_t) == 12u, "能力项布局必须固定");

typedef enum
{
    IW_SNAPSHOT_CLOCK = 1,
    IW_SNAPSHOT_CAPABILITIES = 2,
    IW_SNAPSHOT_SERVICE = 3
} iw_snapshot_topic_t;

typedef struct
{
    uint16_t version;
    uint16_t topic;
    uint16_t header_bytes;
    uint16_t payload_bytes;
    uint32_t session_id;
    uint32_t revision;
    uint32_t flags;
    uint32_t reserved;
} iw_snapshot_header_t;

_Static_assert(sizeof(iw_snapshot_header_t) == 24u, "快照头布局必须固定");

typedef enum
{
    IW_SNAPSHOT_OK = 0,
    IW_SNAPSHOT_INVALID,
    IW_SNAPSHOT_TOPIC_UNKNOWN,
    IW_SNAPSHOT_TOO_SMALL
} iw_snapshot_status_t;

typedef struct
{
    uint32_t submitted;
    uint32_t duplicates;
    uint32_t rejected;
    uint32_t completed;
    uint32_t acknowledged;
    uint32_t accepted_request_high_water;
    uint32_t service_revision;
    uint32_t queue_depth;
    uint32_t active_count;
    uint32_t ledger_used;
} iw_service_stats_t;

typedef struct
{
    uint16_t count;
    uint16_t reserved;
    iw_capability_entry_t entries[IW_CAPABILITY_CAPACITY];
} iw_capability_snapshot_t;

typedef struct
{
    iw_command_t command;
    iw_result_t result;
    uint32_t generation;
    uint8_t used;
    uint8_t reserved[3];
} iw_service_slot_t;

_Static_assert(sizeof(iw_service_slot_t) <= 96u, "单个结果账本槽不得超过 96 字节");

typedef struct
{
    iw_time_state_t *time_state;
    iw_service_slot_t slots[IW_RESULT_LEDGER_CAPACITY];
    iw_capability_entry_t capabilities[IW_CAPABILITY_CAPACITY];
    iw_service_stats_t stats;
    uint8_t queue[IW_COMMAND_CAPACITY];
    uint32_t session_id;
    uint32_t next_slot_generation;
    uint32_t capability_revision;
    uint8_t queue_head;
    uint8_t queue_count;
    uint8_t active_count;
    uint8_t capability_count;
} iw_service_t;

typedef struct
{
    uint32_t session_id;
    uint32_t next_request_id;
} iw_client_session_t;

void iw_command_init(iw_command_t *command,
                     uint32_t session_id,
                     uint32_t request_id,
                     uint16_t page_id,
                     uint32_t page_generation,
                     iw_opcode_t opcode);
bool iw_command_encode_set_clock(iw_command_t *command,
                                 uint32_t utc_seconds,
                                 int16_t offset_minutes,
                                 uint32_t expected_revision);
bool iw_command_decode_set_clock(const iw_command_t *command, iw_set_clock_payload_t *payload);

bool iw_client_session_init(iw_client_session_t *client, uint32_t session_id);
bool iw_client_next_request(iw_client_session_t *client, uint32_t *request_id);

bool iw_service_init(iw_service_t *service, iw_time_state_t *time_state, uint32_t session_id);
uint32_t iw_service_session_id(const iw_service_t *service);
bool iw_service_set_capability(iw_service_t *service,
                               iw_capability_id_t capability_id,
                               iw_capability_state_t state,
                               int32_t last_error);
iw_submit_status_t iw_service_command_submit(iw_service_t *service, const iw_command_t *command);
iw_take_status_t iw_service_take_next(iw_service_t *service, iw_service_work_t *work);
bool iw_service_finish_set_clock(iw_service_t *service,
                                 const iw_result_token_t *token,
                                 uint32_t raw_tick,
                                 bool device_success);
iw_result_lookup_t iw_service_result_get(const iw_service_t *service,
                                         uint32_t session_id,
                                         uint32_t request_id,
                                         iw_result_t *result);
iw_ack_status_t iw_service_result_ack(iw_service_t *service, const iw_result_token_t *token);
bool iw_result_matches_page(const iw_result_t *result, uint16_t page_id, uint32_t page_generation);
iw_snapshot_status_t iw_service_snapshot_read(const iw_service_t *service,
                                              iw_snapshot_topic_t topic,
                                              void *output,
                                              size_t capacity,
                                              size_t *required);
void iw_service_stats_read(const iw_service_t *service, iw_service_stats_t *stats);

#endif
