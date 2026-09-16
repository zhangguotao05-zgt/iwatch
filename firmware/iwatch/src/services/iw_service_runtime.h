#ifndef IW_SERVICE_RUNTIME_H
#define IW_SERVICE_RUNTIME_H

#include "iw_service.h"

int iw_service_runtime_init(void);
uint32_t iw_service_current_session(void);

/* 这些入口均为有界复制；设备访问只在服务线程、模型锁之外执行。 */
iw_submit_status_t iw_command_submit(const iw_command_t *command);
iw_result_lookup_t iw_result_get(uint32_t session_id, uint32_t request_id, iw_result_t *result);
iw_ack_status_t iw_result_ack(const iw_result_token_t *token);
iw_snapshot_status_t iw_snapshot_read(iw_snapshot_topic_t topic,
                                      void *output,
                                      size_t capacity,
                                      size_t *required);
bool iw_clock_read(iw_clock_snapshot_t *snapshot);
void iw_service_runtime_stats(iw_service_stats_t *stats);

#endif
