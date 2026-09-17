#ifndef IW_KEY_PORT_H
#define IW_KEY_PORT_H

#include "iw_keys.h"

typedef struct {
    void (*feedback)(unsigned key, bool pressed);
    bool (*signal)(iw_key_signal_t signal);
    void (*rotate)(int32_t steps);
    void (*cancel)(void);
    /* 只查询驱动快照，不得访问 LVGL；诊断生产者也会调用。 */
    bool (*touch_down)(void);
} iw_key_port_ops_t;

/* GUI 所有者初始化；失败禁用已注册的键，保留 SDK 句柄供同一所有者重试。 */
bool iw_key_port_init(const iw_key_port_ops_t *ops);
void iw_key_port_process(void);
void iw_key_port_cancel(void);
uint32_t iw_key_port_wait_ms(uint32_t other_ms);
iw_input_context_t iw_key_port_context(void);
/* GUI 所有者同步切换；生产者必须使用下方排队接口。 */
bool iw_key_port_apply_context(iw_input_context_t context);
/* 线程侧诊断入口与物理输入共用有界队列，不允许从 ISR 查询触摸互斥快照。 */
bool iw_key_port_inject(unsigned key, bool pressed);
bool iw_key_port_rotate(int32_t steps);
bool iw_key_port_set_context(iw_input_context_t context);

#endif
