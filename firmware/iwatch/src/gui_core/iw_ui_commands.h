#ifndef IW_UI_COMMANDS_H
#define IW_UI_COMMANDS_H
#include "iw_service.h"
#define IW_UI_COMMAND_SLOTS 8u
typedef struct {
    bool (*allocate)(uint32_t *session, uint32_t *request);
    uint32_t (*session)(void);
    iw_submit_status_t (*submit)(const iw_command_t *command);
    iw_result_lookup_t (*get)(uint32_t session, uint32_t request, iw_result_t *result);
    iw_ack_status_t (*ack)(const iw_result_token_t *token);
} iw_ui_command_port_t;
typedef struct {
    iw_command_t command;
    iw_result_t result;
    uint32_t started_ms;
    bool used, detached, terminal, acknowledged;
} iw_ui_command_slot_t;
typedef struct {
    const iw_ui_command_port_t *port;
    iw_ui_command_slot_t slots[IW_UI_COMMAND_SLOTS];
} iw_ui_commands_t;
/* 全部由 GUI 所有者调用；不保存页面指针。退出只解除订阅，不取消已受理命令。 */
iw_submit_status_t iw_ui_command_send(iw_ui_commands_t *client, iw_command_t *command, uint32_t now_ms,
                                      uint32_t *request_id);
void iw_ui_commands_poll(iw_ui_commands_t *client);
void iw_ui_commands_detach(iw_ui_commands_t *client, uint16_t page_id, uint32_t generation);
bool iw_ui_command_take(iw_ui_commands_t *client, uint32_t session, uint32_t request_id, iw_result_t *result);
bool iw_ui_command_delayed(const iw_ui_commands_t *client, uint32_t session, uint32_t request_id,
                           uint32_t now_ms);
bool iw_ui_commands_pending(const iw_ui_commands_t *client);
#endif
