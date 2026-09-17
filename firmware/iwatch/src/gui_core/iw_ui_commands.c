#include "iw_ui_commands.h"
#include <string.h>

iw_submit_status_t iw_ui_command_send(iw_ui_commands_t *c, iw_command_t *command, uint32_t now,
                                      uint32_t *request) {
    if (!c || !c->port || !command || !request) return IW_SUBMIT_INVALID;
    unsigned i = 0;
    while (i < IW_UI_COMMAND_SLOTS && c->slots[i].used)
        i++;
    if (i == IW_UI_COMMAND_SLOTS || !c->port->allocate(&command->session_id, &command->request_id))
        return IW_SUBMIT_BUSY_NO_ADMISSION;
    iw_submit_status_t status = c->port->submit(command);
    if (status != IW_SUBMIT_QUEUED && status != IW_SUBMIT_DUPLICATE) return status;
    c->slots[i] = (iw_ui_command_slot_t){.command = *command, .used = true, .started_ms = now};
    *request = command->request_id;
    return status;
}

void iw_ui_commands_poll(iw_ui_commands_t *c) {
    if (!c || !c->port) return;
    uint32_t session = c->port->session();
    if (!session) return;
    for (unsigned i = 0; i < IW_UI_COMMAND_SLOTS; i++) {
        iw_ui_command_slot_t *s = &c->slots[i];
        if (!s->used) continue;
        if (s->command.session_id != session) {
            s->result = (iw_result_t){.request_id = s->command.request_id,
                                      .page_id = s->command.page_id,
                                      .page_generation = s->command.page_generation,
                                      .state = IW_RESULT_STATE_TERMINAL,
                                      .code = IW_RESULT_SESSION_CHANGED};
            s->terminal = s->acknowledged = true;
        }
        if (!s->terminal) {
            iw_result_t result;
            iw_result_lookup_t status = c->port->get(s->command.session_id, s->command.request_id, &result);
            if (status == IW_RESULT_LOOKUP_FOUND && result.state == IW_RESULT_STATE_TERMINAL &&
                result.session_id == s->command.session_id && result.request_id == s->command.request_id &&
                result.page_id == s->command.page_id &&
                result.page_generation == s->command.page_generation) {
                s->result = result;
                s->terminal = true;
            } else if (status == IW_RESULT_LOOKUP_EXPIRED || status == IW_RESULT_LOOKUP_SESSION_CHANGED) {
                s->result =
                    (iw_result_t){.request_id = s->command.request_id,
                                  .page_id = s->command.page_id,
                                  .page_generation = s->command.page_generation,
                                  .state = IW_RESULT_STATE_TERMINAL,
                                  .code = status == IW_RESULT_LOOKUP_EXPIRED ? IW_RESULT_EXPIRED
                                                                             : IW_RESULT_SESSION_CHANGED};
                s->terminal = s->acknowledged = true;
            }
        }
        if (s->terminal && !s->acknowledged) {
            iw_result_token_t token = {s->result.session_id, s->result.request_id,
                                       s->result.ledger_generation};
            iw_ack_status_t status = c->port->ack(&token);
            s->acknowledged =
                status == IW_ACK_OK || status == IW_ACK_SESSION_CHANGED || status == IW_ACK_NOT_FOUND;
        }
        if (s->terminal && s->acknowledged && s->detached) memset(s, 0, sizeof(*s));
    }
}

void iw_ui_commands_detach(iw_ui_commands_t *c, uint16_t page, uint32_t generation) {
    if (!c) return;
    for (unsigned i = 0; i < IW_UI_COMMAND_SLOTS; i++) {
        iw_ui_command_slot_t *s = &c->slots[i];
        if (s->used && s->command.page_id == page && s->command.page_generation == generation) {
            s->detached = true;
            if (s->terminal && s->acknowledged) memset(s, 0, sizeof(*s));
        }
    }
}

bool iw_ui_command_take(iw_ui_commands_t *c, uint32_t session, uint32_t request, iw_result_t *result) {
    if (!c || !request || !result) return false;
    for (unsigned i = 0; i < IW_UI_COMMAND_SLOTS; i++) {
        iw_ui_command_slot_t *s = &c->slots[i];
        if (s->used && !s->detached && s->command.session_id == session && s->command.request_id == request &&
            s->terminal && s->acknowledged) {
            *result = s->result;
            memset(s, 0, sizeof(*s));
            return true;
        }
    }
    return false;
}

bool iw_ui_command_delayed(const iw_ui_commands_t *c, uint32_t session, uint32_t request, uint32_t now) {
    if (!c) return false;
    for (unsigned i = 0; i < IW_UI_COMMAND_SLOTS; i++)
        if (c->slots[i].used && c->slots[i].command.session_id == session &&
            c->slots[i].command.request_id == request)
            return (uint32_t)(now - c->slots[i].started_ms) >= 2000u;
    return false;
}

bool iw_ui_commands_pending(const iw_ui_commands_t *c) {
    if (c)
        for (unsigned i = 0; i < IW_UI_COMMAND_SLOTS; i++)
            if (c->slots[i].used) return true;
    return false;
}
