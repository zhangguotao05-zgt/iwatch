#ifndef IW_COMPONENTS_DEMO_H
#define IW_COMPONENTS_DEMO_H
#include <stdbool.h>
/* 仅 GUI 主循环调用；串口命令只向邮箱投递，不能直接创建对象。 */
void iw_components_demo_process(void);
bool iw_components_demo_home(void);
#endif
