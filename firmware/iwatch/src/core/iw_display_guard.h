#ifndef IW_DISPLAY_GUARD_H
#define IW_DISPLAY_GUARD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t last_tick;
    bool initialized;
} iw_display_wake_gate_t;

/* 限制重复唤屏命令；无符号节拍差允许系统计数器自然回绕。 */
void iw_display_wake_gate_init(iw_display_wake_gate_t *gate);
bool iw_display_wake_gate_take(iw_display_wake_gate_t *gate, uint32_t now, uint32_t minimum_ticks);

#endif
