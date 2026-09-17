#include <rtthread.h>
#include <rtdevice.h>
#include "littlevgl2rtt.h"
#include "lvgl.h"
// #include "lvsf.h"
#include "gui_app_fwk.h"
#include "time.h"
#include "app_clock_main.h"

extern bool app_clock_main_status_bar_init(lv_obj_t *par, lv_obj_t *clock_tileview);
extern void app_clock_main_status_bar_deinit(void);
extern bool app_clock_main_status_bar_take_font_fault(void);
extern bool app_clock_main_status_bar_font_fault_pending(void);
extern void app_clock_main_status_bar_note_fallback(void);
/* 返回 true 时仍在排空绘制，调用方本轮不得提交新的 LVGL 绘制。 */
extern bool app_clock_main_process_font_fault(void);
