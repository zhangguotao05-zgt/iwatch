#ifndef IW_SERVICE_RUNTIME_H
#define IW_SERVICE_RUNTIME_H

#include "iw_display.h"
#include "iw_service.h"

int iw_service_runtime_init(void);
uint32_t iw_service_current_session(void);
/* GUI 与诊断共享编号分配器，session/request 在同一模型锁内取得。 */
bool iw_request_allocate(uint32_t *session, uint32_t *request);

/* 这些入口均为有界复制；设备访问只在服务线程、模型锁之外执行。 */
iw_submit_status_t iw_command_submit(const iw_command_t *command);
iw_result_lookup_t iw_result_get(uint32_t session_id, uint32_t request_id, iw_result_t *result);
iw_ack_status_t iw_result_ack(const iw_result_token_t *token);
/* required 必须非空；无论查询大小还是读取内容，均返回实际字节数。 */
iw_snapshot_status_t iw_snapshot_read(iw_snapshot_topic_t topic,
                                      void *output,
                                      size_t capacity,
                                      size_t *required);
bool iw_clock_read(iw_clock_snapshot_t *snapshot);
bool iw_brightness_read(iw_brightness_snapshot_t *snapshot);
bool iw_stopwatch_summary_runtime_read(iw_stopwatch_summary_t *summary);
void iw_service_runtime_stats(iw_service_stats_t *stats);

/* LCD 设备访问只能由 GUI owner 完成；以下接口只复制邮箱和模型状态。 */
bool iw_display_runtime_set_available(bool available, int32_t device_error);
iw_display_take_status_t iw_display_take_request(iw_display_request_t *request);
bool iw_display_complete_request(const iw_display_request_t *request,
                                 iw_display_apply_status_t status,
                                 int32_t device_error);
bool iw_display_note_current_apply(uint8_t level,
                                   uint32_t target_revision,
                                   uint32_t target_sequence,
                                   iw_display_apply_status_t status,
                                   int32_t device_error);
void iw_display_runtime_stats(iw_display_mailbox_stats_t *stats);

#endif
