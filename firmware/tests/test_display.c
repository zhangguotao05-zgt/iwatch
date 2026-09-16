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
    test_latest_target_and_exact_completion();
    test_invalid_requests_have_no_side_effect();
    puts("display mailbox tests passed");
    return 0;
}
