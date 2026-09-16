#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "iw_display.h"

static iw_display_request_t request_make(uint32_t request_id,
                                         uint32_t sequence,
                                         uint8_t level,
                                         iw_brightness_kind_t kind)
{
    iw_display_request_t request;

    memset(&request, 0, sizeof(request));
    request.token.session_id = 7u;
    request.token.request_id = request_id;
    request.token.ledger_generation = request_id + 100u;
    request.setting_sequence = sequence;
    request.level = level;
    request.kind = (uint8_t)kind;
    return request;
}

typedef struct
{
    iw_display_driver_state_t before_state;
    iw_display_driver_state_t after_state;
    iw_display_apply_status_t write_status;
    int32_t write_error;
    uint8_t readback;
    bool state_query_ok;
    bool busy_query_ok;
    bool readback_query_ok;
    bool busy;
    unsigned state_reads;
    unsigned writes;
    unsigned readbacks;
} fake_driver_t;

static void fake_driver_init(fake_driver_t *driver, uint8_t level)
{
    memset(driver, 0, sizeof(*driver));
    driver->before_state = IW_DISPLAY_DRIVER_STATE_READY;
    driver->after_state = IW_DISPLAY_DRIVER_STATE_READY;
    driver->write_status = IW_DISPLAY_APPLY_OK;
    driver->readback = level;
    driver->state_query_ok = true;
    driver->busy_query_ok = true;
    driver->readback_query_ok = true;
}

static bool fake_read_state(void *context, iw_display_driver_state_t *state)
{
    fake_driver_t *driver = context;

    driver->state_reads++;
    if (!driver->state_query_ok) return false;
    *state = driver->state_reads == 1u ? driver->before_state : driver->after_state;
    return true;
}

static bool fake_read_busy(void *context, bool *busy)
{
    fake_driver_t *driver = context;

    if (!driver->busy_query_ok) return false;
    *busy = driver->busy;
    return true;
}

static iw_display_apply_status_t fake_write_brightness(void *context,
                                                       uint8_t level,
                                                       int32_t *device_error)
{
    fake_driver_t *driver = context;

    (void)level;
    driver->writes++;
    *device_error = driver->write_error;
    return driver->write_status;
}

static bool fake_read_brightness(void *context, uint8_t *level)
{
    fake_driver_t *driver = context;

    driver->readbacks++;
    if (!driver->readback_query_ok) return false;
    *level = driver->readback;
    return true;
}

static iw_display_driver_ops_t fake_ops(fake_driver_t *driver)
{
    iw_display_driver_ops_t ops;

    ops.context = driver;
    ops.read_state = fake_read_state;
    ops.read_busy = fake_read_busy;
    ops.write_brightness = fake_write_brightness;
    ops.read_brightness = fake_read_brightness;
    return ops;
}

static void test_verified_driver_apply(void)
{
    fake_driver_t driver;
    iw_display_driver_ops_t ops;
    int32_t error;

    fake_driver_init(&driver, 50u);
    ops = fake_ops(&driver);
    assert(iw_display_apply_verified(&ops, 50u, &error) == IW_DISPLAY_APPLY_OK);
    assert(error == 0 && driver.state_reads == 2u && driver.writes == 1u &&
           driver.readbacks == 1u);

    fake_driver_init(&driver, 50u);
    driver.before_state = IW_DISPLAY_DRIVER_STATE_UNAVAILABLE;
    ops = fake_ops(&driver);
    assert(iw_display_apply_verified(&ops, 50u, &error) == IW_DISPLAY_APPLY_FAILED);
    assert(error == IW_DISPLAY_ERROR_UNAVAILABLE && driver.writes == 0u);

    fake_driver_init(&driver, 50u);
    driver.before_state = IW_DISPLAY_DRIVER_STATE_TIMEOUT;
    ops = fake_ops(&driver);
    assert(iw_display_apply_verified(&ops, 50u, &error) == IW_DISPLAY_APPLY_TIMEOUT);
    assert(error == IW_DISPLAY_ERROR_TIMEOUT && driver.writes == 0u);

    fake_driver_init(&driver, 50u);
    driver.busy = true;
    ops = fake_ops(&driver);
    assert(iw_display_apply_verified(&ops, 50u, &error) == IW_DISPLAY_APPLY_BUSY);
    assert(error == IW_DISPLAY_ERROR_BUSY && driver.writes == 0u);

    fake_driver_init(&driver, 50u);
    driver.write_status = IW_DISPLAY_APPLY_BUSY;
    driver.write_error = -7;
    ops = fake_ops(&driver);
    assert(iw_display_apply_verified(&ops, 50u, &error) == IW_DISPLAY_APPLY_BUSY);
    assert(error == -7 && driver.readbacks == 0u);

    fake_driver_init(&driver, 50u);
    driver.after_state = IW_DISPLAY_DRIVER_STATE_TIMEOUT;
    ops = fake_ops(&driver);
    assert(iw_display_apply_verified(&ops, 50u, &error) == IW_DISPLAY_APPLY_TIMEOUT);
    assert(error == IW_DISPLAY_ERROR_TIMEOUT && driver.writes == 1u &&
           driver.readbacks == 0u);

    fake_driver_init(&driver, 49u);
    ops = fake_ops(&driver);
    assert(iw_display_apply_verified(&ops, 50u, &error) == IW_DISPLAY_APPLY_FAILED);
    assert(error == IW_DISPLAY_ERROR_READBACK_MISMATCH && driver.writes == 1u);

    fake_driver_init(&driver, 50u);
    driver.readback_query_ok = false;
    ops = fake_ops(&driver);
    assert(iw_display_apply_verified(&ops, 50u, &error) == IW_DISPLAY_APPLY_FAILED);
    assert(error == IW_DISPLAY_ERROR_READBACK_QUERY);

    fake_driver_init(&driver, 50u);
    driver.state_query_ok = false;
    ops = fake_ops(&driver);
    assert(iw_display_apply_verified(&ops, 50u, &error) == IW_DISPLAY_APPLY_FAILED);
    assert(error == IW_DISPLAY_ERROR_STATE_QUERY && driver.writes == 0u);

    assert(iw_display_apply_verified(NULL, 50u, &error) == IW_DISPLAY_APPLY_FAILED);
    assert(error == IW_DISPLAY_ERROR_INVALID_ADAPTER);
    assert(iw_display_apply_verified(&ops, 4u, &error) == IW_DISPLAY_APPLY_FAILED);
    assert(error == IW_DISPLAY_ERROR_INVALID_ADAPTER);
}

static void test_latest_target_and_exact_completion(void)
{
    iw_display_mailbox_t mailbox;
    iw_display_mailbox_stats_t stats;
    iw_display_request_t first = request_make(1u, 1u, 20u, IW_BRIGHTNESS_PREVIEW);
    iw_display_request_t second = request_make(2u, 2u, 30u, IW_BRIGHTNESS_FINAL);
    iw_display_request_t third = request_make(3u, 3u, 40u, IW_BRIGHTNESS_PREVIEW);
    iw_display_request_t fourth = request_make(4u, 4u, 50u, IW_BRIGHTNESS_FINAL);
    iw_display_request_t replaced;
    iw_display_request_t taken;
    iw_display_request_t wrong;

    iw_display_mailbox_init(&mailbox);
    memset(&stats, 0xA5, sizeof(stats));
    iw_display_mailbox_stats(&mailbox, &stats);
    assert(stats.posted == 0u && stats.pending == 0u && stats.in_flight == 0u);

    assert(iw_display_mailbox_post(&mailbox, &first, &replaced) ==
           IW_DISPLAY_POST_ACCEPTED);
    assert(iw_display_mailbox_post(&mailbox, &first, &replaced) ==
           IW_DISPLAY_POST_STALE);
    assert(iw_display_mailbox_post(&mailbox, &second, &replaced) ==
           IW_DISPLAY_POST_REPLACED);
    assert(replaced.token.request_id == first.token.request_id);

    assert(iw_display_mailbox_take(&mailbox, &taken) == IW_DISPLAY_TAKE_READY);
    assert(taken.token.request_id == second.token.request_id);
    assert(iw_display_mailbox_take(&mailbox, &wrong) == IW_DISPLAY_TAKE_BUSY);

    assert(iw_display_mailbox_post(&mailbox, &third, &replaced) ==
           IW_DISPLAY_POST_ACCEPTED);
    assert(iw_display_mailbox_post(&mailbox, &fourth, &replaced) ==
           IW_DISPLAY_POST_REPLACED);
    assert(replaced.token.request_id == third.token.request_id);

    wrong = taken;
    wrong.token.ledger_generation++;
    assert(!iw_display_mailbox_matches_in_flight(&mailbox, &wrong));
    assert(!iw_display_mailbox_complete(&mailbox, &wrong, IW_DISPLAY_APPLY_OK));
    assert(iw_display_mailbox_matches_in_flight(&mailbox, &taken));
    assert(!iw_display_mailbox_complete(&mailbox, &taken,
                                        (iw_display_apply_status_t)99));
    assert(iw_display_mailbox_complete(&mailbox, &taken, IW_DISPLAY_APPLY_OK));

    assert(iw_display_mailbox_take(&mailbox, &taken) == IW_DISPLAY_TAKE_READY);
    assert(taken.token.request_id == fourth.token.request_id);
    assert(iw_display_mailbox_complete(&mailbox, &taken, IW_DISPLAY_APPLY_TIMEOUT));
    assert(iw_display_mailbox_take(&mailbox, &taken) == IW_DISPLAY_TAKE_EMPTY);

    iw_display_mailbox_stats(&mailbox, &stats);
    assert(stats.posted == 4u);
    assert(stats.replaced == 2u);
    assert(stats.taken == 2u);
    assert(stats.completed == 2u);
    assert(stats.stale == 1u);
    assert(stats.accepted_sequence == 4u);
    assert(stats.apply_ok == 1u && stats.apply_timeout == 1u);
    assert(stats.apply_busy == 0u && stats.apply_failed == 0u);
    assert(stats.pending == 0u && stats.in_flight == 0u);

    assert(iw_display_mailbox_note_apply(&mailbox, IW_DISPLAY_APPLY_BUSY));
    assert(iw_display_mailbox_note_apply(&mailbox, IW_DISPLAY_APPLY_FAILED));
    assert(!iw_display_mailbox_note_apply(&mailbox, (iw_display_apply_status_t)99));
    iw_display_mailbox_stats(&mailbox, &stats);
    assert(stats.apply_busy == 1u && stats.apply_failed == 1u);
}

static void test_invalid_requests_have_no_side_effect(void)
{
    iw_display_mailbox_t mailbox;
    iw_display_mailbox_t before;
    iw_display_request_t invalid = request_make(1u, 1u, 4u, IW_BRIGHTNESS_PREVIEW);

    iw_display_mailbox_init(&mailbox);
    before = mailbox;
    assert(iw_display_mailbox_post(&mailbox, &invalid, NULL) == IW_DISPLAY_POST_INVALID);
    assert(memcmp(&mailbox, &before, sizeof(mailbox)) == 0);
    assert(iw_display_mailbox_post(NULL, &invalid, NULL) == IW_DISPLAY_POST_INVALID);
    assert(iw_display_mailbox_take(NULL, &invalid) == IW_DISPLAY_TAKE_EMPTY);
}

int main(void)
{
    test_verified_driver_apply();
    test_latest_target_and_exact_completion();
    test_invalid_requests_have_no_side_effect();
    puts("display mailbox tests passed");
    return 0;
}
