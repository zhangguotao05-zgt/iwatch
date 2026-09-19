#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "iw_service.h"

static iw_command_t clock_command(uint32_t session,
                                  uint32_t request,
                                  uint32_t revision,
                                  uint16_t page,
                                  uint32_t generation,
                                  uint32_t utc)
{
    iw_command_t command;
    iw_command_init(&command, session, request, page, generation, IW_OPCODE_SET_CLOCK);
    assert(iw_command_encode_set_clock(&command, utc, 480, revision));
    return command;
}

static iw_command_t brightness_command(uint32_t session,
                                       uint32_t request,
                                       uint32_t revision,
                                       uint32_t sequence,
                                       uint8_t level,
                                       iw_brightness_kind_t kind)
{
    iw_command_t command;
    iw_command_init(&command, session, request, 0x0102u, 1u,
                    IW_OPCODE_SET_BRIGHTNESS);
    assert(iw_command_encode_set_brightness(&command, level, kind, revision, sequence));
    return command;
}

static iw_result_t execute_software(iw_service_t *service, const iw_command_t *command)
{
    iw_service_work_t work;
    iw_result_t result;
    iw_result_token_t token;
    assert(iw_service_command_submit(service, command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(service, &work) == IW_TAKE_COMPLETED);
    assert(iw_service_result_get(service, command->session_id, command->request_id,
                                 &result) == IW_RESULT_LOOKUP_FOUND);
    token = (iw_result_token_t){result.session_id, result.request_id,
                                result.ledger_generation};
    assert(iw_service_result_ack(service, &token) == IW_ACK_OK);
    return result;
}

static iw_command_t timer_create_command(uint32_t session, uint32_t request, uint32_t duration_ms)
{
    iw_command_t command;
    iw_command_init(&command, session, request, 0x0200u, 1u,
                    IW_OPCODE_TIMER_CREATE);
    assert(iw_command_encode_timer_create(&command, duration_ms));
    return command;
}

static iw_command_t timer_control_command(uint32_t session, uint32_t request,
                                          iw_opcode_t opcode, uint32_t timer_id,
                                          uint32_t revision)
{
    iw_command_t command;
    iw_command_init(&command, session, request, 0x0202u, 1u, opcode);
    assert(iw_command_encode_timer_control(&command, timer_id, revision));
    return command;
}

static iw_command_t stopwatch_command(uint32_t session, uint32_t request,
                                      iw_opcode_t opcode, uint32_t revision)
{
    iw_command_t command;
    iw_command_init(&command, session, request, 0x0300u, 1u, opcode);
    assert(iw_command_encode_stopwatch_control(&command, revision));
    return command;
}

static iw_result_t finish_one(iw_service_t *service, uint32_t tick, bool device_success)
{
    iw_service_work_t work;
    iw_result_t result;
    iw_take_status_t take = iw_service_take_next(service, &work);

    assert(take == IW_TAKE_WORK);
    assert(iw_service_finish_set_clock(service, &work.token, tick, device_success));
    assert(iw_service_result_get(service, work.token.session_id, work.token.request_id,
                                 &result) == IW_RESULT_LOOKUP_FOUND);
    return result;
}

static void test_duplicate_expiry_and_session(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_command_t command;
    iw_command_t conflict;
    iw_result_t result;
    iw_result_token_t token;
    iw_clock_snapshot_t clock;
    iw_service_t rebuilt;

    assert(iw_time_init(&time_state, 0u, 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 7u));
    command = clock_command(7u, 1u, 1u, 11u, 5u, 1704153600u);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_DUPLICATE);

    conflict = command;
    conflict.page_generation++;
    assert(iw_service_command_submit(&service, &conflict) == IW_SUBMIT_TOKEN_CONFLICT);
    result = finish_one(&service, 20u, true);
    assert(result.state == IW_RESULT_STATE_TERMINAL);
    assert(result.code == IW_RESULT_OK_APPLIED);
    assert(result.model_revision == 2u);
    assert(iw_result_matches_page(&result, 11u, 5u));
    assert(!iw_result_matches_page(&result, 11u, 6u));
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_DUPLICATE);

    token.session_id = result.session_id;
    token.request_id = result.request_id;
    token.ledger_generation = result.ledger_generation;
    assert(iw_service_result_ack(&service, &token) == IW_ACK_OK);
    assert(iw_service_result_get(&service, 7u, 1u, &result) == IW_RESULT_LOOKUP_EXPIRED);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_RESULT_EXPIRED);
    assert(iw_service_result_get(&service, 8u, 1u, &result) == IW_RESULT_LOOKUP_SESSION_CHANGED);
    assert(iw_service_init(&rebuilt, &time_state, 8u));
    assert(iw_service_session_id(&rebuilt) == 8u);
    assert(iw_service_command_submit(&rebuilt, &command) == IW_SUBMIT_SESSION_CHANGED);
    assert(iw_time_read(&time_state, &clock) && clock.revision == 2u);

    command = clock_command(7u, 2u, 1u, 11u, 6u, 1704240000u);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &(iw_service_work_t){0}) == IW_TAKE_COMPLETED);
    assert(iw_service_result_get(&service, 7u, 2u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.code == IW_RESULT_STATE_CONFLICT);
    assert(iw_time_read(&time_state, &clock) && clock.revision == 2u);
}

static void test_queue_and_ledger_capacity(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_command_t command;
    iw_clock_snapshot_t clock;
    iw_service_work_t work;
    iw_result_t result;
    iw_result_token_t token;
    iw_service_stats_t stats;

    assert(iw_time_init(&time_state, 0u, 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 9u));
    for (uint32_t request = 1u; request <= IW_COMMAND_CAPACITY; request++)
    {
        command = clock_command(9u, request, 1u, 1u, request, 1704067200u + request);
        assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    }
    command = clock_command(9u, 17u, 1u, 1u, 17u, 1704067300u);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_BUSY_NO_ADMISSION);
    assert(iw_time_read(&time_state, &clock) && clock.revision == 1u);

    for (unsigned i = 0; i < IW_COMMAND_CAPACITY; i++)
    {
        iw_take_status_t take = iw_service_take_next(&service, &work);
        if (i == 0u)
        {
            assert(take == IW_TAKE_WORK);
            assert(iw_service_finish_set_clock(&service, &work.token, 10u, true));
        }
        else
        {
            assert(take == IW_TAKE_COMPLETED);
        }
    }
    iw_service_stats_read(&service, &stats);
    assert(stats.queue_depth == 0u && stats.active_count == 0u && stats.ledger_used == 16u);

    assert(iw_time_read(&time_state, &clock));
    for (uint32_t request = 17u; request <= 32u; request++)
    {
        command = clock_command(9u, request, clock.revision, 1u, request,
                                1704067200u + request);
        assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    }
    for (unsigned i = 0; i < IW_COMMAND_CAPACITY; i++)
    {
        iw_take_status_t take = iw_service_take_next(&service, &work);
        if (i == 0u)
        {
            assert(take == IW_TAKE_WORK);
            assert(iw_service_finish_set_clock(&service, &work.token, 20u, true));
        }
        else
        {
            assert(take == IW_TAKE_COMPLETED);
        }
    }
    iw_service_stats_read(&service, &stats);
    assert(stats.ledger_used == IW_RESULT_LEDGER_CAPACITY);
    command = clock_command(9u, 33u, 3u, 1u, 33u, 1704067400u);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_BUSY_NO_ADMISSION);
    assert(iw_time_read(&time_state, &clock) && clock.revision == 3u);

    assert(iw_service_result_get(&service, 9u, 1u, &result) == IW_RESULT_LOOKUP_FOUND);
    token.session_id = result.session_id;
    token.request_id = result.request_id;
    token.ledger_generation = result.ledger_generation;
    assert(iw_service_result_ack(&service, &token) == IW_ACK_OK);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
}

static void test_device_failure_has_no_clock_side_effect(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_command_t command;
    iw_clock_snapshot_t before;
    iw_clock_snapshot_t after;
    iw_result_t result;

    assert(iw_time_init(&time_state, 100u, 1000u, 1704067200, 480,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 10u));
    assert(iw_time_read(&time_state, &before));
    command = clock_command(10u, 1u, before.revision, 2u, 1u, 1704153600u);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    result = finish_one(&service, 200u, false);
    assert(result.code == IW_RESULT_DEVICE_FAULT);
    assert(iw_time_read(&time_state, &after));
    assert(after.revision == before.revision);
    assert(after.utc_ms == before.utc_ms);
    assert(after.offset_minutes == before.offset_minutes);
}

static void test_revision_exhaustion_has_no_device_work(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_command_t command;
    iw_result_t result;
    iw_service_work_t work;

    assert(iw_time_init(&time_state, 0u, 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    time_state.revision = UINT32_MAX;
    assert(iw_service_init(&service, &time_state, 11u));
    command = clock_command(11u, 1u, UINT32_MAX, 2u, 1u, 1704153600u);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &work) == IW_TAKE_COMPLETED);
    assert(iw_service_result_get(&service, 11u, 1u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.state == IW_RESULT_STATE_TERMINAL);
    assert(result.code == IW_RESULT_CAPACITY);
    assert(time_state.revision == UINT32_MAX);
}

static void test_snapshots_and_client_exhaustion(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_client_session_t client;
    iw_snapshot_header_t header;
    uint8_t tiny[8];
    uint8_t message[160];
    size_t required = 0u;
    uint32_t request;

    assert(iw_time_init(&time_state, UINT32_C(0xfffffff0), 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 12u));
    assert(iw_service_set_capability(&service, IW_CAP_CLOCK, IW_CAP_STATE_AVAILABLE, 0));
    memset(tiny, 0xA5, sizeof(tiny));
    assert(iw_service_snapshot_read(&service, IW_SNAPSHOT_CLOCK, tiny, sizeof(tiny),
                                    &required) == IW_SNAPSHOT_TOO_SMALL);
    for (unsigned i = 0; i < sizeof(tiny); i++) assert(tiny[i] == 0xA5u);
    assert(required > sizeof(tiny) && required <= sizeof(message));
    assert(iw_service_snapshot_read(&service, IW_SNAPSHOT_CLOCK, message, sizeof(message),
                                    &required) == IW_SNAPSHOT_OK);
    memcpy(&header, message, sizeof(header));
    assert(header.version == 1u && header.topic == IW_SNAPSHOT_CLOCK);
    assert(header.session_id == 12u && (header.flags & 1u));
    assert(iw_service_snapshot_read(&service, IW_SNAPSHOT_CAPABILITIES, message,
                                    sizeof(message), &required) == IW_SNAPSHOT_OK);

    assert(iw_client_session_init(&client, 12u));
    client.next_request_id = UINT32_MAX;
    assert(iw_client_next_request(&client, &request) && request == UINT32_MAX);
    assert(!iw_client_next_request(&client, &request));
}

static void test_capability_revision_exhaustion_has_no_side_effect(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_service_t before;

    assert(iw_time_init(&time_state, 0u, 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 13u));

    service.capability_revision = UINT32_MAX;
    before = service;
    assert(!iw_service_set_capability(&service, IW_CAP_CLOCK,
                                      IW_CAP_STATE_AVAILABLE, 0));
    assert(memcmp(&service, &before, sizeof(service)) == 0);

    service.capability_revision = UINT32_MAX - 1u;
    assert(iw_service_set_capability(&service, IW_CAP_CLOCK,
                                     IW_CAP_STATE_AVAILABLE, 0));
    assert(service.capability_count == 1u);
    assert(service.capability_revision == UINT32_MAX);
    before = service;
    assert(!iw_service_set_capability(&service, IW_CAP_CLOCK,
                                      IW_CAP_STATE_FAULT, -1));
    assert(memcmp(&service, &before, sizeof(service)) == 0);
    assert(iw_service_set_capability(&service, IW_CAP_CLOCK,
                                     IW_CAP_STATE_AVAILABLE, 0));
    assert(memcmp(&service, &before, sizeof(service)) == 0);
}

static void test_brightness_preview_final_and_snapshot(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_command_t command;
    iw_service_work_t work;
    iw_result_t result;
    iw_result_token_t token;
    iw_brightness_snapshot_t brightness;
    iw_snapshot_header_t header;
    uint8_t tiny[8];
    uint8_t message[64];
    size_t required = 0u;

    assert(iw_time_init(&time_state, 0u, 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 21u));
    assert(iw_service_brightness_read(&service, &brightness));
    assert(brightness.desired == IW_BRIGHTNESS_DEFAULT);
    assert(brightness.revision == 1u && brightness.desired_revision == 1u &&
           brightness.setting_sequence == 0u && brightness.applied_sequence == 0u);
    assert(brightness.flags == (IW_BRIGHTNESS_FLAG_DESIRED_VALID |
                                IW_BRIGHTNESS_FLAG_SESSION_ONLY));
    assert(brightness.applied_revision == 0u && brightness.persisted_revision == 0u);

    command = brightness_command(21u, 1u, 1u, 1u, 20u, IW_BRIGHTNESS_PREVIEW);
    assert(iw_service_command_submit(&service, &command) ==
           IW_SUBMIT_CAPABILITY_UNAVAILABLE);
    assert(iw_service_set_capability(&service, IW_CAP_DISPLAY,
                                     IW_CAP_STATE_AVAILABLE, 0));
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_brightness_read(&service, &brightness));
    assert(brightness.desired == 20u && brightness.desired_revision == 2u &&
           brightness.setting_sequence == 1u);

    command = brightness_command(21u, 2u, 2u, 2u, 30u, IW_BRIGHTNESS_PREVIEW);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_result_get(&service, 21u, 1u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.state == IW_RESULT_STATE_TERMINAL && result.code == IW_RESULT_SUPERSEDED);
    assert(result.payload[0] == 30u && result.payload[1] == 0u);
    token = (iw_result_token_t){result.session_id, result.request_id,
                                result.ledger_generation};
    assert(iw_service_result_ack(&service, &token) == IW_ACK_OK);

    command = brightness_command(21u, 3u, 3u, 3u, 40u, IW_BRIGHTNESS_FINAL);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_result_get(&service, 21u, 2u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.state == IW_RESULT_STATE_TERMINAL && result.code == IW_RESULT_SUPERSEDED);
    token = (iw_result_token_t){result.session_id, result.request_id,
                                result.ledger_generation};
    assert(iw_service_result_ack(&service, &token) == IW_ACK_OK);

    assert(iw_service_take_next(&service, &work) == IW_TAKE_WORK);
    assert(work.command.request_id == 3u);
    assert(iw_service_finish_set_brightness(&service, &work.token, 25u,
                                            IW_RESULT_OK_APPLIED, 0));
    assert(iw_service_brightness_read(&service, &brightness));
    assert(brightness.desired == 40u && brightness.applied == 40u);
    assert(brightness.revision == 5u);
    assert(brightness.desired_revision == 4u && brightness.applied_revision == 4u);
    assert(brightness.setting_sequence == 3u && brightness.applied_sequence == 3u);
    assert(!(brightness.flags & IW_BRIGHTNESS_FLAG_PERSISTED_VALID));
    assert(brightness.flags & IW_BRIGHTNESS_FLAG_SESSION_ONLY);
    assert(iw_service_result_get(&service, 21u, 3u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.state == IW_RESULT_STATE_TERMINAL && result.code == IW_RESULT_OK_APPLIED);
    assert(result.model_revision == 4u && result.completed_mono_ms == 25u);
    assert(result.payload[0] == 40u && result.payload[1] == 40u);

    memset(tiny, 0xA5, sizeof(tiny));
    assert(iw_service_snapshot_read(&service, IW_SNAPSHOT_BRIGHTNESS, tiny, sizeof(tiny),
                                    &required) == IW_SNAPSHOT_TOO_SMALL);
    for (unsigned i = 0; i < sizeof(tiny); i++) assert(tiny[i] == 0xA5u);
    assert(required == sizeof(iw_snapshot_header_t) + sizeof(iw_brightness_snapshot_t));
    assert(iw_service_snapshot_read(&service, IW_SNAPSHOT_BRIGHTNESS, message,
                                    sizeof(message), &required) == IW_SNAPSHOT_OK);
    memcpy(&header, message, sizeof(header));
    assert(header.topic == IW_SNAPSHOT_BRIGHTNESS && header.revision == brightness.revision);
    assert(header.payload_bytes == sizeof(iw_brightness_snapshot_t) && (header.flags & 1u));

    iw_command_init(&command, 21u, 4u, 0x0102u, 1u, IW_OPCODE_SET_BRIGHTNESS);
    assert(!iw_command_encode_set_brightness(&command, 4u, IW_BRIGHTNESS_PREVIEW, 4u, 4u));
    assert(!iw_command_encode_set_brightness(&command, 101u, IW_BRIGHTNESS_PREVIEW, 4u, 4u));
}

static void test_brightness_failures_preserve_applied_state(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_command_t command;
    iw_service_work_t work;
    iw_result_t result;
    iw_brightness_snapshot_t brightness;

    assert(iw_time_init(&time_state, 0u, 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 22u));
    assert(iw_service_set_capability(&service, IW_CAP_DISPLAY,
                                     IW_CAP_STATE_AVAILABLE, 0));

    command = brightness_command(22u, 1u, 1u, 1u, 20u, IW_BRIGHTNESS_PREVIEW);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &work) == IW_TAKE_WORK);
    command = brightness_command(22u, 2u, 2u, 2u, 30u, IW_BRIGHTNESS_FINAL);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_finish_set_brightness(&service, &work.token, 10u,
                                            IW_RESULT_OK_APPLIED, 0));
    assert(iw_service_brightness_read(&service, &brightness));
    assert(brightness.desired == 30u && brightness.desired_revision == 3u);
    assert(brightness.applied == 20u && brightness.applied_revision == 2u);

    assert(iw_service_take_next(&service, &work) == IW_TAKE_WORK);
    assert(iw_service_finish_set_brightness(&service, &work.token, 20u,
                                            IW_RESULT_DEVICE_BUSY, -7));
    assert(iw_service_result_get(&service, 22u, 2u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.code == IW_RESULT_DEVICE_BUSY);
    assert(iw_service_brightness_read(&service, &brightness));
    assert(brightness.applied == 20u && brightness.applied_revision == 2u);
    assert(brightness.last_error == -7);

    command = brightness_command(22u, 3u, 3u, 3u, 40u, IW_BRIGHTNESS_PREVIEW);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &work) == IW_TAKE_WORK);
    assert(iw_service_finish_set_brightness(&service, &work.token, 30u,
                                            IW_RESULT_UNCONFIRMED_TIMEOUT, -2));
    assert(iw_service_brightness_read(&service, &brightness));
    assert(brightness.applied == 20u && brightness.last_error == -2);

    command = brightness_command(22u, 4u, 4u, 4u, 50u, IW_BRIGHTNESS_FINAL);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &work) == IW_TAKE_WORK);
    assert(iw_service_finish_set_brightness(&service, &work.token, 40u,
                                            IW_RESULT_DEVICE_FAULT, -1));
    assert(iw_service_brightness_read(&service, &brightness));
    assert(brightness.applied == 20u && brightness.last_error == -1);

    assert(iw_service_note_brightness_applied(&service, 50u, 5u, 4u, true, 0));
    assert(iw_service_brightness_read(&service, &brightness));
    assert(brightness.applied == 50u && brightness.applied_revision == 5u &&
           brightness.applied_sequence == 4u && brightness.last_error == 0);
    assert(!iw_service_note_brightness_applied(&service, 25u, 4u, 3u, true, 0));
    assert(!iw_service_note_brightness_applied(&service, 50u, 5u, 3u, false, -8));
    assert(!iw_service_note_brightness_applied(&service, 49u, 5u, 4u, false, -8));
    assert(iw_service_brightness_read(&service, &brightness));
    assert(brightness.applied == 50u && brightness.applied_revision == 5u &&
           brightness.last_error == 0);
    assert(iw_service_note_brightness_applied(&service, 50u, 5u, 4u, false, -9));
    assert(iw_service_brightness_read(&service, &brightness) && brightness.last_error == -9);
    assert(iw_service_note_brightness_applied(&service, 50u, 5u, 4u, true, 0));
    assert(iw_service_brightness_read(&service, &brightness) && brightness.last_error == 0);
}

static void test_brightness_conflicts_and_revision_exhaustion(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_command_t command;
    iw_service_work_t work;
    iw_result_t result;
    iw_brightness_snapshot_t before;
    iw_brightness_snapshot_t after;

    assert(iw_time_init(&time_state, 0u, 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 23u));
    assert(iw_service_set_capability(&service, IW_CAP_DISPLAY,
                                     IW_CAP_STATE_AVAILABLE, 0));
    assert(iw_service_brightness_read(&service, &before));

    command = brightness_command(23u, 1u, 2u, 1u, 20u, IW_BRIGHTNESS_PREVIEW);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &work) == IW_TAKE_COMPLETED);
    assert(iw_service_result_get(&service, 23u, 1u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.code == IW_RESULT_STATE_CONFLICT && result.model_revision == 1u);
    assert(iw_service_brightness_read(&service, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    command = brightness_command(23u, 2u, 1u, 1u, 30u, IW_BRIGHTNESS_FINAL);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &work) == IW_TAKE_WORK);
    assert(iw_service_finish_set_brightness(&service, &work.token, 10u,
                                            IW_RESULT_OK_APPLIED, 0));
    assert(iw_service_brightness_read(&service, &before));

    command = brightness_command(23u, 3u, 2u, 1u, 40u, IW_BRIGHTNESS_PREVIEW);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &work) == IW_TAKE_COMPLETED);
    assert(iw_service_result_get(&service, 23u, 3u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.code == IW_RESULT_STATE_CONFLICT);
    assert(iw_service_brightness_read(&service, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    service.brightness.desired_revision = UINT32_MAX;
    before = service.brightness;
    command = brightness_command(23u, 4u, UINT32_MAX, 2u, 50u, IW_BRIGHTNESS_FINAL);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &work) == IW_TAKE_COMPLETED);
    assert(iw_service_result_get(&service, 23u, 4u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.code == IW_RESULT_CAPACITY);
    assert(iw_service_brightness_read(&service, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    service.brightness.revision = 10u;
    service.brightness.desired_revision = 10u;
    service.brightness.setting_sequence = UINT32_MAX;
    before = service.brightness;
    command = brightness_command(23u, 5u, 10u, UINT32_MAX, 60u,
                                 IW_BRIGHTNESS_FINAL);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &work) == IW_TAKE_COMPLETED);
    assert(iw_service_result_get(&service, 23u, 5u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.code == IW_RESULT_CAPACITY);
    assert(iw_service_brightness_read(&service, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    service.brightness.revision = UINT32_MAX - 1u;
    service.brightness.desired_revision = 10u;
    service.brightness.setting_sequence = 10u;
    service.brightness.desired = 60u;
    before = service.brightness;
    command = brightness_command(23u, 6u, 10u, 11u, 70u,
                                 IW_BRIGHTNESS_FINAL);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    assert(iw_service_take_next(&service, &work) == IW_TAKE_COMPLETED);
    assert(iw_service_result_get(&service, 23u, 6u, &result) == IW_RESULT_LOOKUP_FOUND);
    assert(result.code == IW_RESULT_CAPACITY);
    assert(iw_service_brightness_read(&service, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    service.brightness.revision = UINT32_MAX;
    service.brightness.desired_revision = 12u;
    service.brightness.setting_sequence = 12u;
    service.brightness.desired = 80u;
    service.brightness.applied = 60u;
    service.brightness.applied_revision = 10u;
    service.brightness.applied_sequence = 10u;
    service.brightness.last_error = -8;
    before = service.brightness;
    assert(!iw_service_note_brightness_applied(&service, 80u, 12u, 12u, true, 0));
    assert(iw_service_brightness_read(&service, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    service.brightness.applied = 80u;
    service.brightness.applied_revision = 12u;
    service.brightness.applied_sequence = 12u;
    service.brightness.last_error = 0;
    before = service.brightness;
    assert(iw_service_note_brightness_applied(&service, 80u, 12u, 12u, true, 0));
    assert(iw_service_brightness_read(&service, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);
}

static void test_brightness_rejection_has_no_model_side_effect(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_command_t command;
    iw_brightness_snapshot_t before;
    iw_brightness_snapshot_t after;

    assert(iw_time_init(&time_state, 0u, 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 24u));
    assert(iw_service_set_capability(&service, IW_CAP_DISPLAY,
                                     IW_CAP_STATE_AVAILABLE, 0));
    assert(iw_service_brightness_read(&service, &before));

    command = brightness_command(24u, 1u, 1u, 1u, 20u, IW_BRIGHTNESS_PREVIEW);
    command.payload[0] = 4u;
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_INVALID);
    assert(iw_service_brightness_read(&service, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    for (uint32_t request = 1u; request <= IW_COMMAND_CAPACITY; request++)
    {
        command = clock_command(24u, request, 1u, 1u, request,
                                1704067200u + request);
        assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_QUEUED);
    }
    command = brightness_command(24u, IW_COMMAND_CAPACITY + 1u, 1u, 1u, 30u,
                                 IW_BRIGHTNESS_FINAL);
    assert(iw_service_command_submit(&service, &command) == IW_SUBMIT_BUSY_NO_ADMISSION);
    assert(iw_service_brightness_read(&service, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);
}

static void test_d11_service_t16_deadline_and_snapshots(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_command_t command;
    iw_result_t result;
    iw_timer_snapshot_t timers;
    uint8_t message[sizeof(iw_snapshot_header_t) + sizeof(iw_timer_snapshot_t)];
    size_t required = 0u;

    assert(iw_time_init(&time_state, 0u, 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 31u));
    command = timer_create_command(31u, 1u, 5000u);
    result = execute_software(&service, &command);
    assert(result.code == IW_RESULT_OK_APPLIED);
    uint32_t timer_id = (uint32_t)result.payload[0] |
                        ((uint32_t)result.payload[1] << 8) |
                        ((uint32_t)result.payload[2] << 16) |
                        ((uint32_t)result.payload[3] << 24);
    uint32_t revision = result.model_revision;

    /* 日历校时只改 UTC 映射；同一单调截止时刻仍必须先于暂停生效。 */
    assert(iw_time_set_clock(&time_state, 1000u, 1893456000, 480,
                             IW_TIME_SOURCE_MANUAL) == IW_TIME_OK);
    iw_time_sample(&time_state, 4999u);
    assert(iw_service_timers_read(&service, 4999u, &timers));
    assert(timers.timers[0].state == IW_TIMER_RUNNING &&
           timers.timers[0].remaining_ms == 1u);
    iw_time_sample(&time_state, 5000u);
    command = timer_control_command(31u, 2u, IW_OPCODE_TIMER_PAUSE, timer_id, revision);
    result = execute_software(&service, &command);
    assert(result.code == IW_RESULT_STATE_CONFLICT);
    assert(iw_service_timers_read(&service, 5000u, &timers));
    assert(timers.count == 1u && timers.timers[0].state == IW_TIMER_EXPIRED &&
           timers.timers[0].alert_pending);
    assert(iw_service_snapshot_read(&service, IW_SNAPSHOT_TIMERS, message,
                                    sizeof(message), &required) == IW_SNAPSHOT_OK);
    assert(required == sizeof(iw_snapshot_header_t) + offsetof(iw_timer_snapshot_t, timers) +
                       sizeof(iw_timer_view_t));
}

static void test_d11_service_t17_background_and_lap_capacity(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_command_t command;
    iw_result_t result;
    iw_stopwatch_snapshot_t stopwatch;
    uint32_t request = 1u;

    assert(iw_time_init(&time_state, 0u, 1000u, 1704067200, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 32u));
    command = timer_create_command(32u, request++, 1000u);
    assert(execute_software(&service, &command).code == IW_RESULT_OK_APPLIED);
    iw_time_sample(&time_state, 1000u);
    assert(iw_service_advance(&service, 1000u) == 1u);
    assert(iw_service_advance(&service, 2000u) == 0u);

    assert(iw_service_stopwatch_read(&service, 2000u, &stopwatch));
    command = stopwatch_command(32u, request++, IW_OPCODE_STOPWATCH_START,
                                stopwatch.revision);
    assert(execute_software(&service, &command).code == IW_RESULT_OK_APPLIED);
    for (unsigned i = 0; i < IW_STOPWATCH_LAP_CAPACITY; i++) {
        assert(iw_service_stopwatch_read(&service, 2001u + i, &stopwatch));
        iw_time_sample(&time_state, 2001u + i);
        command = stopwatch_command(32u, request++, IW_OPCODE_STOPWATCH_LAP,
                                    stopwatch.revision);
        assert(execute_software(&service, &command).code == IW_RESULT_OK_APPLIED);
    }
    assert(iw_service_stopwatch_read(&service, 3000u, &stopwatch));
    command = stopwatch_command(32u, request++, IW_OPCODE_STOPWATCH_LAP,
                                stopwatch.revision);
    result = execute_software(&service, &command);
    assert(result.code == IW_RESULT_CAPACITY);
    assert(iw_service_stopwatch_read(&service, 4000u, &stopwatch));
    assert(stopwatch.state == IW_STOPWATCH_RUNNING &&
           stopwatch.lap_count == IW_STOPWATCH_LAP_CAPACITY);
}

static void test_d12_t18_simultaneous_sources(void)
{
    iw_time_state_t time_state;
    iw_service_t service;
    iw_alarm_edit_t edit = {0};
    iw_alert_snapshot_t alerts;
    iw_alarm_snapshot_t alarms;
    iw_timer_snapshot_t timers;
    iw_command_t command;
    iw_result_t result;
    int64_t utc_seconds = 0;
    uint32_t request = 1u, handle;

    assert(iw_time_calendar_to_utc_seconds(2026, 9u, 18u, 7u, 59u, 0u, &utc_seconds));
    assert(iw_time_init(&time_state, 0u, 1000u, utc_seconds, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, 42u));
    for (unsigned i = 0; i < IW_TIMER_CAPACITY; i++) {
        command = timer_create_command(42u, request++, 60000u);
        assert(execute_software(&service, &command).code == IW_RESULT_OK_APPLIED);
    }
    edit.hour = 8u;
    edit.enabled = 1u;
    edit.weekday_mask = 0x7fu;
    memcpy(edit.label, "Alarm", sizeof("Alarm"));
    for (unsigned i = 0; i < IW_ALARM_CAPACITY; i++) {
        assert(iw_service_alarm_draft_store(&service, &edit, &handle));
        iw_command_init(&command, 42u, request++, 0x0400u, 1u, IW_OPCODE_ALARM_APPLY);
        assert(iw_command_encode_alarm_apply(&command, handle, 0u));
        assert(execute_software(&service, &command).code == IW_RESULT_OK_APPLIED);
    }
    assert(iw_service_alarms_read(&service, &alarms) && alarms.count == IW_ALARM_CAPACITY);
    iw_time_sample(&time_state, 60000u);
    assert(iw_service_advance(&service, 60000u) == IW_ALERT_CAPACITY);
    assert(iw_service_alerts_read(&service, &alerts) && alerts.count == IW_ALERT_CAPACITY);
    assert(iw_service_advance(&service, 60000u) == 0u);

    assert(iw_service_timers_read(&service, 60000u, &timers));
    iw_command_init(&command, 42u, request++, 0x0401u, 1u, IW_OPCODE_ACK_ALERT);
    assert(iw_command_encode_alert_control(&command, IW_ALERT_SOURCE_TIMER,
                                           timers.timers[0].timer_id,
                                           timers.timers[0].occurrence));
    result = execute_software(&service, &command);
    assert(result.code == IW_RESULT_OK_APPLIED);
    assert(iw_service_timers_read(&service, 60000u, &timers));
    assert(!timers.timers[0].alert_pending);
    assert(iw_service_advance(&service, 60000u) == 0u);
    assert(iw_service_alerts_read(&service, &alerts) && alerts.count == IW_ALERT_CAPACITY - 1u);
}

int main(void)
{
    test_duplicate_expiry_and_session();
    test_queue_and_ledger_capacity();
    test_device_failure_has_no_clock_side_effect();
    test_revision_exhaustion_has_no_device_work();
    test_snapshots_and_client_exhaustion();
    test_capability_revision_exhaustion_has_no_side_effect();
    test_brightness_preview_final_and_snapshot();
    test_brightness_failures_preserve_applied_state();
    test_brightness_conflicts_and_revision_exhaustion();
    test_brightness_rejection_has_no_model_side_effect();
    test_d11_service_t16_deadline_and_snapshots();
    test_d11_service_t17_background_and_lap_capacity();
    test_d12_t18_simultaneous_sources();
    puts("service tests passed");
    return 0;
}
