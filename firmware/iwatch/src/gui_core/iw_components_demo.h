#ifndef IW_COMPONENTS_DEMO_H
#define IW_COMPONENTS_DEMO_H
#include <stdbool.h>
/* 仅 GUI 主循环调用；串口命令只向邮箱投递，不能直接创建对象。 */
/* 返回 true 时仍在等待渲染排空；主循环须暂停新绘制并在1ms后重试。 */
bool iw_components_demo_process(void);
bool iw_components_demo_home(void);
bool iw_components_demo_active(void);
#endif
