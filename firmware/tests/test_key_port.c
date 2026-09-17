#include "iw_key_port.h"
#include "iw_gui_port.h"
#include <rtthread.h>
#include <rthw.h>
#include <button.h>
#include <stdio.h>

static uint32_t tick;
static unsigned irq, registered, enabled, disabled, wakeups, cancels, presses, deliveries;
static int enable_failure = -1;
static bool physical_pressed[2], touching, change_page, inject_during_gpio, cancel_requested;
static iw_key_signal_t last_signal;
static int32_t rotated;
static button_cfg_t configs[2];
static rt_thread_t thread = (void *)1;
uint32_t rt_tick_get(void) { return tick; }
int32_t rt_tick_from_millisecond(int32_t ms) { return ms; }
rt_thread_t rt_thread_self(void) { return thread; }
rt_base_t rt_hw_interrupt_disable(void) { return irq++; }
void rt_hw_interrupt_enable(rt_base_t old) { assert(irq == old + 1); irq = old; }
int rt_kprintf(const char *format, ...) { (void)format; assert(!irq); return 0; }
void iw_gui_wake(uint32_t reason) { assert(!irq && reason && reason <= 7); wakeups++; }
bool iw_gui_take_cancel(void) { bool value = cancel_requested; cancel_requested = false; return value; }
int32_t button_init(const button_cfg_t *cfg)
{
    assert(!irq && registered < 2 && cfg->active_state == BUTTON_ACTIVE_HIGH && cfg->debounce_time == 20);
    configs[registered] = *cfg;
    return (int32_t)registered++;
}
int button_enable(int32_t id) { assert(!irq); if (id == enable_failure) return -1; enabled |= 1u << id; return 0; }
int button_disable(int32_t id) { assert(!irq); enabled &= ~(1u << id); disabled++; return 0; }
bool button_is_pressed(int32_t id)
{
    assert(!irq && id >= 0 && id < 2);
    if (inject_during_gpio) {
        inject_during_gpio = false;
        assert(iw_key_port_inject(0, false));
    }
    return physical_pressed[id];
}
static void on_feedback(unsigned key, bool down) { assert(!irq && key < 2); if (down) presses++; }
static void on_cancel(void) { assert(!irq); cancels++; }
static bool on_touch(void) { assert(!irq); return touching; }
static void on_rotate(int32_t steps) { assert(!irq && steps >= -32 && steps <= 32); rotated = steps; }
static bool on_signal(iw_key_signal_t value)
{
    assert(!irq); deliveries++; last_signal = value;
    if (value.kind == IW_KEY_UNLOCK) { assert(iw_key_port_apply_context(IW_INPUT_NORMAL)); return true; }
    return change_page;
}
/* 编译真实生产适配层，替换的只有 RTOS 和 GPIO 边界。 */
#include "../iwatch/src/platform/iw_key_port.c"

static const iw_key_port_ops_t ops = {on_feedback, on_signal, on_rotate, on_cancel, on_touch};
static void poll(uint32_t delta) { tick += delta; iw_key_port_process(); }
static void release_all(void)
{
    physical_pressed[0] = physical_pressed[1] = false;
    poll(0); poll(20); poll(20);
}
static void edge(unsigned key, bool down, uint32_t delta)
{
    tick += delta; physical_pressed[key] = down;
    configs[key].button_handler(configs[key].pin, down ? BUTTON_PRESSED : BUTTON_RELEASED);
    iw_key_port_process();
}
static void click(unsigned key)
{
    edge(key, true, 1); edge(key, false, 30); poll(280);
}
int main(void)
{
    assert(!iw_key_port_init(NULL));
    enable_failure = 1;
    assert(!iw_key_port_init(&ops) && registered == 2 && !enabled && disabled == 2);
    configs[0].button_handler(150, BUTTON_PRESSED); poll(0); assert(!presses);
    enable_failure = -1;
    assert(iw_key_port_init(&ops) && registered == 2 && enabled == 3);
    thread = (void *)2; assert(!iw_key_port_init(&ops)); thread = (void *)1;
    release_all();
    for (unsigned loop = 0; loop < 1000; loop++) {
        unsigned base = deliveries;
        edge(0, true, 1); edge(0, false, 30);
        configs[0].button_handler(150, BUTTON_CLICKED); configs[0].button_handler(150, BUTTON_LONG_PRESSED);
        poll(0); assert(deliveries == base && iw_key_port_wait_ms(1000) == 280);
        poll(279); assert(deliveries == base && iw_key_port_wait_ms(1000) == 1);
        poll(1); assert(deliveries == base + 1 && last_signal.kind == IW_KEY_SINGLE);
        edge(0, true, 1); edge(0, false, 30); edge(0, true, 279); edge(0, false, 100);
        assert(deliveries == base + 2 && last_signal.kind == IW_KEY_DOUBLE);
        poll(300); assert(deliveries == base + 2);
        edge(0, true, 1); edge(1, true, 0); poll(800);
        assert(last_signal.key == 0 && last_signal.kind == IW_KEY_HOLD);
        poll(700); assert(last_signal.key == 1 && last_signal.kind == IW_KEY_HOLD);
        edge(0, false, 1); edge(1, false, 0); poll(300); assert(deliveries == base + 4);
        /* 溢出丢掉释放及后续按压：全部取消，物理键仍按住时不能产生动作。 */
        edge(0, true, 1);
        for (unsigned i = 0; i < IW_INPUT_CAPACITY; i++) assert(iw_key_port_inject(1, (i & 1) == 0));
        assert(!iw_key_port_inject(0, false)); assert(!iw_key_port_inject(0, true));
        poll(2000); poll(2000); assert(deliveries == base + 4 && recognizer.keys[0].wait_release);
        release_all(); click(0); assert(deliveries == base + 5);
        /* 一个动作改变页面后，另一个已经到期的动作不能穿透。 */
        edge(0, true, 1); edge(1, true, 0); edge(0, false, 20); edge(1, false, 0);
        change_page = true; poll(280); change_page = false;
        assert(deliveries == base + 6); release_all();
        /* 水锁的解锁同步提交，不能被紧随其后的取消冲掉。 */
        assert(iw_key_port_set_context(IW_INPUT_WATER)); poll(0); release_all();
        edge(0, true, 1); poll(1999); assert(deliveries == base + 6); poll(1);
        assert(deliveries == base + 7 && last_signal.kind == IW_KEY_UNLOCK && iw_key_port_context() == IW_INPUT_NORMAL);
        edge(0, false, 1); release_all(); poll(300); assert(deliveries == base + 7);
        rotated = 0; touching = true; assert(!iw_key_port_rotate(32)); touching = false;
        assert(iw_key_port_rotate(INT32_MAX)); poll(0); assert(rotated == 32);
        assert(iw_key_port_rotate(INT32_MIN)); touching = true; poll(0); assert(rotated == 32); touching = false;
        /* GPIO 采样期间若到达旧边沿，必须先消费队列，再推进当前时刻。 */
        edge(0, true, 1); inject_during_gpio = true; poll(40);
        assert(input_queue.count == 1); physical_pressed[0] = false; poll(0); poll(280);
        assert(last_signal.kind == IW_KEY_SINGLE && deliveries == base + 8);
        cancel_requested = true; poll(0); release_all();
        assert(!irq);
    }
    int32_t value;
    assert(number("-2147483648", &value) && value == INT32_MIN);
    assert(number("2147483647", &value) && value == INT32_MAX);
    assert(!number("2147483648", &value) && !number("-2147483649", &value));
    assert(!number("--1", &value) && !number("1x", &value));
    assert(!iw_key_port_inject(2, true) && !iw_key_port_set_context((iw_input_context_t)7));
    char *bad[] = {"iw_key", "rotate", "-2147483649"}; iw_key(3, bad);
    iw_input_stat(); iw_input_stat_reset();
    assert(cancels && presses && wakeups && !irq && registered == 2);
    puts("D09 key port: 1000 hardware-edge, deadline, overflow, page-cancel, water-unlock and rotation passes; GPIO outside IRQ");
    return 0;
}
