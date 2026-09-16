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

int main(void)
{
    test_duplicate_expiry_and_session();
    test_queue_and_ledger_capacity();
    test_device_failure_has_no_clock_side_effect();
    test_revision_exhaustion_has_no_device_work();
    test_snapshots_and_client_exhaustion();
    puts("service tests passed");
    return 0;
}
