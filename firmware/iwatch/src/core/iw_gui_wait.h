#ifndef IW_GUI_WAIT_H
#define IW_GUI_WAIT_H
#include <stdint.h>
#define IW_GUI_ACTIVE_MAX_MS 20u
/* 返回值单位为 RTOS 节拍；零等待至少让出一拍，无定时器时仍保持输入服务。 */
uint32_t iw_gui_wait_ticks(uint32_t lvgl_ms, uint32_t ticks_per_second);
#endif
