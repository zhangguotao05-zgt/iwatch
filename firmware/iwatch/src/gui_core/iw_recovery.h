#ifndef IW_RECOVERY_H
#define IW_RECOVERY_H

#include <stdbool.h>

/* 所有接口仅在 GUI 线程调用。首次创建必须早于演示页面的资源分配。 */
bool iw_recovery_init(void);
void iw_recovery_show(const char *owner);
void iw_recovery_hide(const char *owner);

/* GUI 线程读取当前应急层状态。 */
bool iw_recovery_visible(void);
#endif
