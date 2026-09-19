#ifndef IW_ALERT_OUTPUT_H
#define IW_ALERT_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "iw_alerts.h"

#define IW_ALERT_OUTPUT_MAX_MS 30000u

typedef enum {
    IW_ALERT_OUTPUT_VISUAL = 0,
    IW_ALERT_OUTPUT_HAPTIC,
    IW_ALERT_OUTPUT_SOUND
} iw_alert_output_channel_t;

typedef struct {
    uint64_t started_mono_ms;
    uint32_t entity_id;
    uint32_t occurrence;
    uint8_t source_type;
    uint8_t channel;
    uint8_t active;
    uint8_t timed_out;
} iw_alert_output_t;

void iw_alert_output_init(iw_alert_output_t *output);
bool iw_alert_output_step(iw_alert_output_t *output, iw_alerts_t *alerts,
                          uint64_t now_mono_ms, bool sound_available,
                          bool haptic_available, bool silent);

#endif
