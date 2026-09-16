#include "iw_display_guard.h"

void iw_display_wake_gate_init(iw_display_wake_gate_t *gate)
{
    gate->last_tick = 0;
    gate->initialized = false;
}

bool iw_display_wake_gate_take(iw_display_wake_gate_t *gate, uint32_t now, uint32_t minimum_ticks)
{
    if (gate->initialized && (uint32_t)(now - gate->last_tick) < minimum_ticks)
        return false;
    gate->last_tick = now;
    gate->initialized = true;
    return true;
}
