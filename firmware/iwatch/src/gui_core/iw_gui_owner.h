#ifndef IW_GUI_OWNER_H
#define IW_GUI_OWNER_H

#include <stdbool.h>
#include <stdint.h>

/* 页面持有稳定地址的零初始化节点；回调先停定时器/订阅，再删对象并释放字体。 */
typedef struct iw_gui_owner {
    struct iw_gui_owner *next;
    void (*stop)(void *context);
    void *context;
} iw_gui_owner_t;

/* 仅 GUI 线程调用。故障期间拒绝登记；重复登记失败，重复移除成功。 */
bool iw_gui_owner_add(iw_gui_owner_t *owner, void (*stop)(void *), void *context);
bool iw_gui_owner_remove(iw_gui_owner_t *owner);
/* 绘制失败只锁存；不在绘制回调内删除对象或清空字体缓存。 */
void iw_gui_fault_raise(void);
bool iw_gui_fault_pending(void);
/* GUI 主循环安全点调用；true 表示仍在等待，不应开始下一轮绘制。 */
bool iw_gui_fault_process(void);
void iw_gui_fault_dismiss(void);

#endif
