#ifndef IW_DISPLAY_H
#define IW_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "iw_service.h"

typedef struct
{
    iw_result_token_t token;
    uint32_t setting_sequence;
    uint8_t level;
    uint8_t kind;
    uint16_t reserved;
} iw_display_request_t;

_Static_assert(sizeof(iw_display_request_t) == 20u, "显示请求必须保持 20 字节");

typedef enum
{
    IW_DISPLAY_POST_ACCEPTED = 0,
    IW_DISPLAY_POST_REPLACED,
    IW_DISPLAY_POST_STALE,
    IW_DISPLAY_POST_INVALID
} iw_display_post_status_t;

typedef enum
{
    IW_DISPLAY_TAKE_EMPTY = 0,
    IW_DISPLAY_TAKE_READY,
    IW_DISPLAY_TAKE_BUSY
} iw_display_take_status_t;

typedef enum
{
    IW_DISPLAY_APPLY_OK = 0,
    IW_DISPLAY_APPLY_BUSY,
    IW_DISPLAY_APPLY_TIMEOUT,
    IW_DISPLAY_APPLY_FAILED
} iw_display_apply_status_t;

typedef struct
{
    uint32_t posted;
    uint32_t replaced;
    uint32_t taken;
    uint32_t completed;
    uint32_t stale;
    uint32_t accepted_sequence;
    uint32_t apply_ok;
    uint32_t apply_busy;
    uint32_t apply_timeout;
    uint32_t apply_failed;
    uint8_t pending;
    uint8_t in_flight;
    uint16_t reserved;
} iw_display_mailbox_stats_t;

typedef struct
{
    iw_display_request_t pending_request;
    iw_display_request_t in_flight_request;
    iw_display_mailbox_stats_t stats;
} iw_display_mailbox_t;

/* mailbox 本身不加锁；运行时必须用同一把短临界区互斥锁串行调用。 */
void iw_display_mailbox_init(iw_display_mailbox_t *mailbox);
iw_display_post_status_t iw_display_mailbox_post(iw_display_mailbox_t *mailbox,
                                                 const iw_display_request_t *request,
                                                 iw_display_request_t *replaced_request);
iw_display_take_status_t iw_display_mailbox_take(iw_display_mailbox_t *mailbox,
                                                  iw_display_request_t *request);
bool iw_display_mailbox_matches_in_flight(const iw_display_mailbox_t *mailbox,
                                           const iw_display_request_t *request);
bool iw_display_mailbox_complete(iw_display_mailbox_t *mailbox,
                                 const iw_display_request_t *request,
                                 iw_display_apply_status_t status);
bool iw_display_mailbox_note_apply(iw_display_mailbox_t *mailbox,
                                   iw_display_apply_status_t status);
void iw_display_mailbox_stats(const iw_display_mailbox_t *mailbox,
                              iw_display_mailbox_stats_t *stats);

#endif
