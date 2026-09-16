#include "iw_display.h"

#include <limits.h>
#include <string.h>

static void count_add(uint32_t *value)
{
    if (*value != UINT32_MAX) (*value)++;
}

static bool token_equal(const iw_result_token_t *left, const iw_result_token_t *right)
{
    return left->session_id == right->session_id &&
           left->request_id == right->request_id &&
           left->ledger_generation == right->ledger_generation;
}

static bool request_valid(const iw_display_request_t *request)
{
    return request && request->token.session_id != 0u && request->token.request_id != 0u &&
           request->token.ledger_generation != 0u && request->setting_sequence != 0u &&
           request->level >= IW_BRIGHTNESS_MIN && request->level <= IW_BRIGHTNESS_MAX &&
           (request->kind == IW_BRIGHTNESS_PREVIEW || request->kind == IW_BRIGHTNESS_FINAL) &&
           request->reserved == 0u;
}

static iw_display_apply_status_t driver_state_status(iw_display_driver_state_t state,
                                                      int32_t *device_error)
{
    switch (state)
    {
    case IW_DISPLAY_DRIVER_STATE_READY:
        return IW_DISPLAY_APPLY_OK;
    case IW_DISPLAY_DRIVER_STATE_BUSY:
        *device_error = IW_DISPLAY_ERROR_BUSY;
        return IW_DISPLAY_APPLY_BUSY;
    case IW_DISPLAY_DRIVER_STATE_TIMEOUT:
        *device_error = IW_DISPLAY_ERROR_TIMEOUT;
        return IW_DISPLAY_APPLY_TIMEOUT;
    case IW_DISPLAY_DRIVER_STATE_UNAVAILABLE:
        *device_error = IW_DISPLAY_ERROR_UNAVAILABLE;
        return IW_DISPLAY_APPLY_FAILED;
    default:
        *device_error = IW_DISPLAY_ERROR_STATE_QUERY;
        return IW_DISPLAY_APPLY_FAILED;
    }
}

iw_display_apply_status_t iw_display_apply_verified(const iw_display_driver_ops_t *ops,
                                                     uint8_t level,
                                                     int32_t *device_error)
{
    iw_display_driver_state_t state = IW_DISPLAY_DRIVER_STATE_UNKNOWN;
    iw_display_apply_status_t status;
    bool busy = true;
    uint8_t applied = 0u;

    if (!device_error) return IW_DISPLAY_APPLY_FAILED;
    *device_error = 0;
    if (!ops || !ops->read_state || !ops->read_busy || !ops->write_brightness ||
            !ops->read_brightness || level < IW_BRIGHTNESS_MIN ||
            level > IW_BRIGHTNESS_MAX)
    {
        *device_error = IW_DISPLAY_ERROR_INVALID_ADAPTER;
        return IW_DISPLAY_APPLY_FAILED;
    }

    if (!ops->read_state(ops->context, &state))
    {
        *device_error = IW_DISPLAY_ERROR_STATE_QUERY;
        return IW_DISPLAY_APPLY_FAILED;
    }
    status = driver_state_status(state, device_error);
    if (status != IW_DISPLAY_APPLY_OK) return status;

    if (!ops->read_busy(ops->context, &busy))
    {
        *device_error = IW_DISPLAY_ERROR_BUSY_QUERY;
        return IW_DISPLAY_APPLY_FAILED;
    }
    if (busy)
    {
        *device_error = IW_DISPLAY_ERROR_BUSY;
        return IW_DISPLAY_APPLY_BUSY;
    }

    status = ops->write_brightness(ops->context, level, device_error);
    if (status > IW_DISPLAY_APPLY_FAILED)
    {
        *device_error = IW_DISPLAY_ERROR_CONTROL;
        return IW_DISPLAY_APPLY_FAILED;
    }
    if (status != IW_DISPLAY_APPLY_OK)
    {
        if (*device_error == 0)
        {
            if (status == IW_DISPLAY_APPLY_BUSY)
                *device_error = IW_DISPLAY_ERROR_BUSY;
            else if (status == IW_DISPLAY_APPLY_TIMEOUT)
                *device_error = IW_DISPLAY_ERROR_TIMEOUT;
            else
                *device_error = IW_DISPLAY_ERROR_CONTROL;
        }
        return status;
    }

    state = IW_DISPLAY_DRIVER_STATE_UNKNOWN;
    if (!ops->read_state(ops->context, &state))
    {
        *device_error = IW_DISPLAY_ERROR_STATE_QUERY;
        return IW_DISPLAY_APPLY_FAILED;
    }
    status = driver_state_status(state, device_error);
    if (status != IW_DISPLAY_APPLY_OK) return status;

    if (!ops->read_brightness(ops->context, &applied))
    {
        *device_error = IW_DISPLAY_ERROR_READBACK_QUERY;
        return IW_DISPLAY_APPLY_FAILED;
    }
    if (applied != level)
    {
        *device_error = IW_DISPLAY_ERROR_READBACK_MISMATCH;
        return IW_DISPLAY_APPLY_FAILED;
    }

    *device_error = 0;
    return IW_DISPLAY_APPLY_OK;
}

void iw_display_mailbox_init(iw_display_mailbox_t *mailbox)
{
    if (mailbox) memset(mailbox, 0, sizeof(*mailbox));
}

iw_display_post_status_t iw_display_mailbox_post(iw_display_mailbox_t *mailbox,
                                                 const iw_display_request_t *request,
                                                 iw_display_request_t *replaced_request)
{
    bool replaced;

    if (!mailbox || !request_valid(request)) return IW_DISPLAY_POST_INVALID;
    if (request->setting_sequence <= mailbox->stats.accepted_sequence)
    {
        count_add(&mailbox->stats.stale);
        return IW_DISPLAY_POST_STALE;
    }

    replaced = mailbox->stats.pending != 0u;
    if (replaced && replaced_request) *replaced_request = mailbox->pending_request;
    mailbox->pending_request = *request;
    mailbox->stats.pending = 1u;
    mailbox->stats.accepted_sequence = request->setting_sequence;
    count_add(&mailbox->stats.posted);
    if (replaced)
    {
        count_add(&mailbox->stats.replaced);
        return IW_DISPLAY_POST_REPLACED;
    }
    return IW_DISPLAY_POST_ACCEPTED;
}

iw_display_take_status_t iw_display_mailbox_take(iw_display_mailbox_t *mailbox,
                                                 iw_display_request_t *request)
{
    if (!mailbox || !request) return IW_DISPLAY_TAKE_EMPTY;
    if (mailbox->stats.in_flight) return IW_DISPLAY_TAKE_BUSY;
    if (!mailbox->stats.pending) return IW_DISPLAY_TAKE_EMPTY;

    mailbox->in_flight_request = mailbox->pending_request;
    memset(&mailbox->pending_request, 0, sizeof(mailbox->pending_request));
    mailbox->stats.pending = 0u;
    mailbox->stats.in_flight = 1u;
    *request = mailbox->in_flight_request;
    count_add(&mailbox->stats.taken);
    return IW_DISPLAY_TAKE_READY;
}

bool iw_display_mailbox_matches_in_flight(const iw_display_mailbox_t *mailbox,
                                          const iw_display_request_t *request)
{
    return mailbox && request && mailbox->stats.in_flight &&
           request->setting_sequence == mailbox->in_flight_request.setting_sequence &&
           token_equal(&request->token, &mailbox->in_flight_request.token);
}

bool iw_display_mailbox_complete(iw_display_mailbox_t *mailbox,
                                 const iw_display_request_t *request,
                                 iw_display_apply_status_t status)
{
    if (!iw_display_mailbox_matches_in_flight(mailbox, request) ||
            status > IW_DISPLAY_APPLY_FAILED)
        return false;

    memset(&mailbox->in_flight_request, 0, sizeof(mailbox->in_flight_request));
    mailbox->stats.in_flight = 0u;
    count_add(&mailbox->stats.completed);
    (void)iw_display_mailbox_note_apply(mailbox, status);
    return true;
}

bool iw_display_mailbox_note_apply(iw_display_mailbox_t *mailbox,
                                   iw_display_apply_status_t status)
{
    if (!mailbox || status > IW_DISPLAY_APPLY_FAILED) return false;
    switch (status)
    {
    case IW_DISPLAY_APPLY_OK:
        count_add(&mailbox->stats.apply_ok);
        break;
    case IW_DISPLAY_APPLY_BUSY:
        count_add(&mailbox->stats.apply_busy);
        break;
    case IW_DISPLAY_APPLY_TIMEOUT:
        count_add(&mailbox->stats.apply_timeout);
        break;
    default:
        count_add(&mailbox->stats.apply_failed);
        break;
    }
    return true;
}

void iw_display_mailbox_stats(const iw_display_mailbox_t *mailbox,
                              iw_display_mailbox_stats_t *stats)
{
    if (mailbox && stats) *stats = mailbox->stats;
}
