#include "iw_ui_commands.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static iw_service_t service;
static iw_time_state_t time_state;
static iw_client_session_t allocator;
static bool fail_ack;
static uint32_t session(void) {
    return service.session_id;
}
static bool allocate(uint32_t *s, uint32_t *r) {
    *s = session();
    if (allocator.session_id != *s) assert(iw_client_session_init(&allocator, *s));
    return iw_client_next_request(&allocator, r);
}
static iw_submit_status_t submit(const iw_command_t *c) {
    return iw_service_command_submit(&service, c);
}
static iw_result_lookup_t get(uint32_t s, uint32_t r, iw_result_t *result) {
    return iw_service_result_get(&service, s, r, result);
}
static iw_ack_status_t ack(const iw_result_token_t *t) {
    return fail_ack ? IW_ACK_INVALID : iw_service_result_ack(&service, t);
}
static const iw_ui_command_port_t port = {allocate, session, submit, get, ack};
static void init(uint32_t id) {
    assert(iw_time_init(&time_state, 0, 1000, IW_TIME_MIN_UTC_SECONDS, 0, IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &time_state, id));
    assert(iw_service_set_capability(&service, IW_CAP_CLOCK, IW_CAP_STATE_AVAILABLE, 0));
}
static iw_command_t command(uint32_t generation) {
    iw_command_t c;
    iw_command_init(&c, 1, 1, 0x0103, generation, IW_OPCODE_SET_CLOCK);
    assert(iw_command_encode_set_clock(&c, (uint32_t)IW_TIME_MIN_UTC_SECONDS, 487, time_state.revision));
    return c;
}
static void finish(void) {
    iw_service_work_t work;
    iw_take_status_t status;
    while ((status = iw_service_take_next(&service, &work)) != IW_TAKE_EMPTY)
        if (status == IW_TAKE_WORK) assert(iw_service_finish_set_clock(&service, &work.token, 0, true));
}
int main(void) {
    init(1);
    iw_ui_commands_t client = {.port = &port};
    for (unsigned cycle = 0; cycle < 1000; cycle++) {
        iw_command_t c = command(cycle + 1);
        uint32_t request = 0;
        assert(iw_ui_command_send(&client, &c, UINT32_MAX - 1000u, &request) == IW_SUBMIT_QUEUED);
        assert(iw_ui_command_delayed(&client, c.session_id, request, 1000));
        iw_ui_commands_detach(&client, c.page_id, c.page_generation);
        assert(iw_ui_commands_pending(&client));
        iw_ui_commands_poll(&client);
        assert(iw_ui_commands_pending(&client));
        finish();
        fail_ack = true;
        iw_ui_commands_poll(&client);
        assert(iw_ui_commands_pending(&client));
        fail_ack = false;
        iw_ui_commands_poll(&client);
        assert(!iw_ui_commands_pending(&client));
        iw_result_t result;
        assert(!iw_ui_command_take(&client, c.session_id, request, &result));
        c = command(cycle + 1001);
        assert(iw_ui_command_send(&client, &c, 0, &request) == IW_SUBMIT_QUEUED);
        finish();
        iw_ui_commands_poll(&client);
        assert(iw_ui_command_take(&client, c.session_id, request, &result));
        assert(result.code == IW_RESULT_OK_APPLIED && result.page_generation == cycle + 1001);
        assert(!iw_ui_commands_pending(&client));
    }
    iw_command_t old = command(3000);
    uint32_t request;
    assert(iw_ui_command_send(&client, &old, 0, &request) == IW_SUBMIT_QUEUED);
    init(2);
    iw_ui_commands_poll(&client);
    iw_command_t current = command(3001);
    assert(iw_ui_command_send(&client, &current, 0, &request) == IW_SUBMIT_QUEUED);
    finish();
    iw_ui_commands_poll(&client);
    iw_result_t result;
    assert(iw_ui_command_take(&client, current.session_id, current.request_id, &result) &&
           result.code == IW_RESULT_OK_APPLIED);
    assert(iw_ui_command_take(&client, old.session_id, old.request_id, &result) &&
           result.code == IW_RESULT_SESSION_CHANGED);
    for (unsigned i = 0; i < IW_UI_COMMAND_SLOTS; i++) {
        current = command(4000 + i);
        assert(iw_ui_command_send(&client, &current, 0, &request) == IW_SUBMIT_QUEUED);
    }
    request = UINT32_MAX;
    current = command(5000);
    assert(iw_ui_command_send(&client, &current, 0, &request) == IW_SUBMIT_BUSY_NO_ADMISSION &&
           request == UINT32_MAX);
    for (unsigned i = 0; i < IW_UI_COMMAND_SLOTS; i++)
        iw_ui_commands_detach(&client, 0x0103, 4000 + i);
    finish();
    iw_ui_commands_poll(&client);
    assert(!iw_ui_commands_pending(&client));
    puts("ui_commands: 1000 detached/ACK/reentry cycles; timeout/session/capacity passed");
    return 0;
}
