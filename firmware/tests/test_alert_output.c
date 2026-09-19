#include "iw_alert_output.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    iw_alerts_t alerts;
    iw_alert_output_t output;
    iw_alert_record_t selected;
    assert(iw_alerts_init(&alerts));
    iw_alert_output_init(&output);
    assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_TIMER, 1u, 1u, 100u, 0u) == IW_ALERT_OK);
    assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_ALARM, 2u, 2u, 100u, 100u) == IW_ALERT_OK);
    assert(iw_alert_output_step(&output, &alerts, 100u, false, false, false));
    assert(output.active && output.entity_id == 1u && output.channel == IW_ALERT_OUTPUT_VISUAL);
    assert(!iw_alert_output_step(&output, &alerts, 30099u, false, false, false));
    assert(iw_alert_output_step(&output, &alerts, 30100u, false, false, false));
    assert(output.timed_out && output.channel == IW_ALERT_OUTPUT_VISUAL);
    assert(!iw_alert_output_step(&output, &alerts, 40000u, false, false, false));
    assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_TIMER, 1u, 1u) == IW_ALERT_OK);
    assert(iw_alert_output_step(&output, &alerts, 40000u, true, true, true));
    assert(output.entity_id == 2u && output.channel == IW_ALERT_OUTPUT_HAPTIC);
    assert(iw_alerts_select(&alerts, &selected) && selected.entity_id == 2u);
    assert(iw_alerts_snooze(&alerts, IW_ALERT_SOURCE_ALARM, 2u, 2u, 40000u) == IW_ALERT_OK);
    assert(iw_alert_output_step(&output, &alerts, 40001u, true, true, false));
    assert(!output.active);
    assert(iw_alerts_advance(&alerts, 40000u + IW_ALERT_SNOOZE_MS) == 1u);
    assert(iw_alert_output_step(&output, &alerts, 40000u + IW_ALERT_SNOOZE_MS,
                                true, true, false));
    assert(output.channel == IW_ALERT_OUTPUT_SOUND);
    puts("D12 输出互斥和 30 秒上限通过");
    return 0;
}
