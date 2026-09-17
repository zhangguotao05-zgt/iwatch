#ifndef IW_TOUCH_INPUT_H
#define IW_TOUCH_INPUT_H

#include <stdbool.h>

/* 只在 GUI 线程调用；初始化要求本板只有一个物理指针设备。 */
bool iw_touch_input_init(void);
/* 读回调和主循环共用取消入口；返回是否执行了溢出取消。 */
bool iw_touch_input_service(void);

#endif
