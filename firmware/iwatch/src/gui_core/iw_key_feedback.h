#ifndef IW_KEY_FEEDBACK_H
#define IW_KEY_FEEDBACK_H
#include <stdbool.h>
/* GUI 线程专用；常驻反馈仅初始化时分配，不使用字体或动画。 */
bool iw_key_feedback_init(void);
void iw_key_feedback_set(unsigned key, bool pressed);
void iw_key_feedback_cancel(void);
#endif
