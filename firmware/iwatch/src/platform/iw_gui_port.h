#ifndef IW_GUI_PORT_H
#define IW_GUI_PORT_H
#include <stdbool.h>
#include <stdint.h>
#define IW_GUI_WAKE_INPUT 1u
#define IW_GUI_WAKE_STATE 2u
#define IW_GUI_WAKE_CANCEL 4u
int iw_gui_port_init(void);
void iw_gui_port_deinit(void);
/* 初始化早于输入注册；唤醒只发送事件，不执行 LVGL。允许 ISR 调用。 */
void iw_gui_wake(uint32_t reason);
void iw_gui_wait(uint32_t lvgl_ms);
/* 请求可跨线程，消费和 LVGL 重置只能发生在 GUI 线程。 */
void iw_gui_cancel_input(void);
bool iw_gui_take_cancel(void);
#endif
