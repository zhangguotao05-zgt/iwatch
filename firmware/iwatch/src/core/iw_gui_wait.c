#include "iw_gui_wait.h"
uint32_t iw_gui_wait_ticks(uint32_t lvgl_ms, uint32_t ticks_per_second)
{
    uint32_t ms = lvgl_ms > IW_GUI_ACTIVE_MAX_MS ? IW_GUI_ACTIVE_MAX_MS : lvgl_ms;
    uint64_t ticks = ((uint64_t)ms * ticks_per_second + 999u) / 1000u;
    return ticks ? (uint32_t)ticks : 1u;
}
