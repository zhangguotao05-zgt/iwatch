#include "iw_alert_output.h"

#include <string.h>

static bool current_exists(const iw_alert_output_t *output, const iw_alerts_t *alerts)
{
    for (unsigned i = 0; i < IW_ALERT_CAPACITY; i++) {
        const iw_alert_record_t *record = &alerts->records[i];
        if (record->source_type == output->source_type &&
            record->entity_id == output->entity_id &&
            record->occurrence == output->occurrence &&
            record->state == IW_ALERT_PRESENTING) return true;
    }
    return false;
}

void iw_alert_output_init(iw_alert_output_t *output)
{
    if (output) memset(output, 0, sizeof(*output));
}

bool iw_alert_output_step(iw_alert_output_t *output, iw_alerts_t *alerts,
                          uint64_t now_mono_ms, bool sound_available,
                          bool haptic_available, bool silent)
{
    iw_alert_record_t record;
    bool stopped = false;
    if (!output || !alerts) return false;
    if (output->active && !current_exists(output, alerts)) {
        iw_alert_output_init(output);
        stopped = true;
    }
    if (output->active) {
        if (!output->timed_out && now_mono_ms >= output->started_mono_ms &&
            now_mono_ms - output->started_mono_ms >= IW_ALERT_OUTPUT_MAX_MS) {
            output->channel = IW_ALERT_OUTPUT_VISUAL;
            output->timed_out = 1u;
            return true;
        }
        return stopped;
    }
    if (!iw_alerts_select(alerts, &record)) return stopped;
    if (record.state == IW_ALERT_PENDING) {
        if (iw_alerts_present(alerts, (iw_alert_source_t)record.source_type,
                              record.entity_id, record.occurrence) != IW_ALERT_OK)
            return stopped;
    } else if (record.state != IW_ALERT_PRESENTING) {
        return stopped;
    }
    output->source_type = record.source_type;
    output->entity_id = record.entity_id;
    output->occurrence = record.occurrence;
    output->started_mono_ms = now_mono_ms;
    output->channel = record.state == IW_ALERT_PRESENTING ? IW_ALERT_OUTPUT_VISUAL :
                      sound_available && !silent ? IW_ALERT_OUTPUT_SOUND :
                      haptic_available ? IW_ALERT_OUTPUT_HAPTIC : IW_ALERT_OUTPUT_VISUAL;
    output->active = 1u;
    output->timed_out = record.state == IW_ALERT_PRESENTING;
    return true;
}
