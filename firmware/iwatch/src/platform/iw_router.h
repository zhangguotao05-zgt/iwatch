#ifndef IW_ROUTER_H
#define IW_ROUTER_H

#include <stdbool.h>

/* 仅在 GUI 线程、框架与字体服务初始化后调用。 */
void iw_router_init(void);
/* GUI 安全点推进，true 要求先排空绘制，不开始下一帧。 */
bool iw_router_process(void);
/* D08 诊断页的临时 KEY1 返回；D09 再接统一的语义输入仲裁。 */
bool iw_router_back(void);

#endif
