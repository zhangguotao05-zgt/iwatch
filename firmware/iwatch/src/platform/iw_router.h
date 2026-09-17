#ifndef IW_ROUTER_H
#define IW_ROUTER_H

#include <stdbool.h>
#include <stdint.h>

/* 仅在 GUI 线程、框架与字体服务初始化后调用。 */
void iw_router_init(void);
/* GUI 安全点推进，true 要求先排空绘制，不开始下一帧。 */
bool iw_router_process(void);
/* Back 只退一层；Home 锁定应用根目标，等待框架安全完成。 */
bool iw_router_back(void);
bool iw_router_home(void);
/* 应急返回始终回桌面，禁止因来源是桌面而反向进入表盘。 */
bool iw_router_recover(void);
bool iw_router_rotate(int32_t steps);
/* 正式页面只允许在 GUI 所有者请求；生命周期消息由两个内置根应用转发。 */
bool iw_router_open(uint16_t page_id);
void iw_router_root_event(uint16_t page_id, unsigned message);
uint32_t iw_router_wait_ms(void);

#endif
