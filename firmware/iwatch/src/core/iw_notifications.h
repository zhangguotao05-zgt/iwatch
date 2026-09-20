#ifndef IW_NOTIFICATIONS_H
#define IW_NOTIFICATIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { IW_NOTIFICATION_CAPACITY = 32, IW_NOTIFICATION_TEXT_BYTES = 256 };
typedef enum {
    IW_NOTIFICATION_LOCAL = 1,
    IW_NOTIFICATION_DIAGNOSTIC = 2
} iw_notification_source_t;
typedef enum {
    IW_NOTIFICATION_OK,
    IW_NOTIFICATION_INVALID,
    IW_NOTIFICATION_STALE,
    IW_NOTIFICATION_EXHAUSTED
} iw_notification_result_t;
typedef struct {
    uint32_t id, revision, sequence, received_utc;
    uint8_t source, time_valid, read, reserved;
    char text[IW_NOTIFICATION_TEXT_BYTES];
} iw_notification_t;
typedef struct {
    iw_notification_t records[IW_NOTIFICATION_CAPACITY];
    uint32_t last_id, last_sequence, last_revision, evicted;
    uint8_t count;
} iw_notification_store_t;

/* 所有入口由同一个所有者串行调用；调用方负责跨线程消息投递。 */
iw_notification_result_t iw_notification_insert(iw_notification_store_t *store,
    iw_notification_source_t source, const char *text, size_t bytes,
    bool time_valid, uint32_t received_utc, uint32_t *id);
iw_notification_result_t iw_notification_mark_read(iw_notification_store_t *store,
    uint32_t id, uint32_t expected_revision);
iw_notification_result_t iw_notification_delete(iw_notification_store_t *store,
    uint32_t id, uint32_t expected_revision);
const iw_notification_t *iw_notification_find(const iw_notification_store_t *store, uint32_t id);

#endif
