#include "iw_key_port.h"
#include "iw_input_queue.h"
#include "iw_gui_port.h"
#include <rtthread.h>
#include <rthw.h>
#include "button.h"
#include <string.h>

static iw_input_queue_t input_queue;
static iw_keys_t recognizer;
static iw_key_port_ops_t callbacks;
static rt_thread_t owner;
static int32_t handles[IW_KEY_COUNT] = {-1, -1};
static bool hardware_enabled, ready;
static iw_input_context_t context;
static uint32_t key_sequences[IW_KEY_COUNT];
static const int32_t pins[IW_KEY_COUNT] = {BSP_KEY1_PIN,
#ifdef BSP_KEY2_PIN
    BSP_KEY2_PIN
#else
    -1
#endif
};
static const button_active_state_t polarities[IW_KEY_COUNT] = {
#ifdef BSP_KEY1_ACTIVE_HIGH
    BUTTON_ACTIVE_HIGH,
#else
    BUTTON_ACTIVE_LOW,
#endif
#ifdef BSP_KEY2_ACTIVE_HIGH
    BUTTON_ACTIVE_HIGH
#else
    BUTTON_ACTIVE_LOW
#endif
};
#define IW_INPUT_LATENCY_LIMIT_MS 100u
#define IW_INPUT_LATENCY_BUCKETS  (IW_INPUT_LATENCY_LIMIT_MS + 1u)

static uint32_t input_latency_histogram[IW_INPUT_LATENCY_BUCKETS];
static uint32_t input_latency_count;
static uint32_t input_latency_max_ms;

/* 末桶表示 100 ms 及以上；ge_100 表示 P95 落入末桶，并非超限样本数量。 */
static void input_latency_record(uint32_t queued_tick)
{
    uint32_t elapsed_ticks = (uint32_t)((uint32_t)rt_tick_get() - queued_tick);
    uint32_t elapsed_ms = (uint32_t)(((uint64_t)elapsed_ticks * 1000u + RT_TICK_PER_SECOND - 1u) /
                                     RT_TICK_PER_SECOND);
    uint32_t bucket = elapsed_ms < IW_INPUT_LATENCY_LIMIT_MS ? elapsed_ms : IW_INPUT_LATENCY_LIMIT_MS;

    if (input_latency_count == UINT32_MAX) return;
    input_latency_count++;
    input_latency_histogram[bucket]++;
    if (elapsed_ms > input_latency_max_ms) input_latency_max_ms = elapsed_ms;
}

static uint32_t input_latency_p95(bool *at_or_above_limit)
{
    uint64_t target = ((uint64_t)input_latency_count * 95u + 99u) / 100u;
    uint32_t accumulated = 0;

    *at_or_above_limit = false;
    if (!target) return 0;
    for (uint32_t i = 0; i < IW_INPUT_LATENCY_BUCKETS; i++)
    {
        accumulated += input_latency_histogram[i];
        if (accumulated >= target)
        {
            *at_or_above_limit = (i == IW_INPUT_LATENCY_LIMIT_MS);
            return i;
        }
    }
    *at_or_above_limit = true;
    return IW_INPUT_LATENCY_LIMIT_MS;
}


static bool post(iw_input_action_t action, int32_t value)
{
    rt_base_t level = rt_hw_interrupt_disable();
    iw_input_event_t event = {(uint32_t)rt_tick_get(), value, action};
    bool accepted = ready && iw_input_queue_push(&input_queue, event);
    rt_hw_interrupt_enable(level);
    if (ready) iw_gui_wake(IW_GUI_WAKE_INPUT);
    return accepted;
}

static void button_event_handler(int32_t pin, button_action_t action)
{
    /* SDK 合成的 CLICK/LONG 不进入队列，避免与项目识别器重复执行。 */
    if (action != BUTTON_PRESSED && action != BUTTON_RELEASED) return;
    rt_base_t level = rt_hw_interrupt_disable();
    if (hardware_enabled) {
        for (unsigned key = 0; key < IW_KEY_COUNT; key++) if (pins[key] == pin) {
            iw_input_event_t event = {(uint32_t)rt_tick_get(), (int32_t)key,
                action == BUTTON_PRESSED ? IW_INPUT_PRESS : IW_INPUT_RELEASE};
            (void)iw_input_queue_push(&input_queue, event);
            break;
        }
    }
    rt_hw_interrupt_enable(level);
    iw_gui_wake(IW_GUI_WAKE_INPUT);
}

bool iw_key_port_init(const iw_key_port_ops_t *ops)
{
    if (!ops || !ops->signal || !ops->cancel || !ops->touch_down || !ops->rotate || !ops->feedback) return false;
    if (owner && owner != rt_thread_self()) return false;
    owner = rt_thread_self();
    rt_base_t level = rt_hw_interrupt_disable();
    hardware_enabled = ready = false;
    rt_hw_interrupt_enable(level);
    callbacks = *ops;
    iw_keys_config_t timing = {(uint32_t)rt_tick_from_millisecond(280),
        {(uint32_t)rt_tick_from_millisecond(800), (uint32_t)rt_tick_from_millisecond(1500)},
        (uint32_t)rt_tick_from_millisecond(2000), (uint32_t)rt_tick_from_millisecond(20)};
    if (!iw_keys_init(&recognizer, &timing)) return false;
    iw_keys_cancel(&recognizer);
    context = IW_INPUT_NORMAL;
    level = rt_hw_interrupt_disable();
    iw_input_queue_init(&input_queue);
    rt_memset(input_latency_histogram, 0, sizeof(input_latency_histogram));
    input_latency_count = input_latency_max_ms = 0;
    rt_memset(key_sequences, 0, sizeof(key_sequences));
    rt_hw_interrupt_enable(level);
    bool success = true;
    for (unsigned key = 0; key < IW_KEY_COUNT; key++) {
        if (pins[key] < 0) continue;
        if (handles[key] < 0) {
            button_cfg_t config = {0};
            config.pin = pins[key];
            config.active_state = polarities[key];
            config.mode = PIN_MODE_INPUT;
            config.debounce_time = 20;
            config.button_handler = button_event_handler;
            handles[key] = button_init(&config);
        }
        if (handles[key] < 0 || button_enable(handles[key]) != SF_EOK) { success = false; break; }
    }
    if (!success) {
        /* SDK 没有释放注册槽的接口；保留句柄，不在重试时重复注册同一引脚。 */
        for (unsigned key = 0; key < IW_KEY_COUNT; key++) if (handles[key] >= 0)
            if (button_disable(handles[key]) != SF_EOK) rt_kprintf("key disable failed key=%u\n", key);
    }
    level = rt_hw_interrupt_disable();
    hardware_enabled = success;
    ready = true;
    rt_hw_interrupt_enable(level);
    rt_kprintf("keys enabled=%u key1=%ld key2=%ld rotation=software_only\n", success, (long)handles[0], (long)handles[1]);
    return success;
}

void iw_key_port_cancel(void)
{
    if (!ready) return;
    RT_ASSERT(rt_thread_self() == owner);
    iw_keys_cancel(&recognizer);
    rt_base_t level = rt_hw_interrupt_disable();
    iw_input_queue_cancel(&input_queue);
    rt_hw_interrupt_enable(level);
    callbacks.cancel();
    iw_gui_wake(IW_GUI_WAKE_CANCEL);
}

static bool deliver(iw_key_signal_t *signals, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        rt_kprintf("key semantic key=%u kind=%u context=%u\n", signals[i].key, signals[i].kind, context);
        if (callbacks.signal(signals[i])) {
            /* 第一个语义改变页面后，其余结果不得作用于新页面。 */
            iw_key_port_cancel();
            return true;
        }
    }
    return false;
}

void iw_key_port_process(void)
{
    if (!ready) return;
    RT_ASSERT(rt_thread_self() == owner);
    uint32_t started = (uint32_t)rt_tick_get();
    if (iw_gui_take_cancel()) iw_key_port_cancel();
    bool changed = false;
    for (unsigned count = 0; count < 8; count++) {
        iw_input_event_t event;
        rt_base_t level = rt_hw_interrupt_disable();
        bool received = iw_input_queue_pop(&input_queue, &event);
        rt_hw_interrupt_enable(level);
        if (!received) break;
        if (event.action == IW_INPUT_CANCEL) {
            iw_keys_cancel(&recognizer);
            callbacks.cancel();
        } else {
            input_latency_record(event.tick);
            iw_key_signal_t signals[IW_KEY_OUTPUTS];
            if (event.action == IW_INPUT_ROTATE) {
                iw_keys_rotate(&recognizer, event.pin, callbacks.touch_down());
            } else if (event.action == IW_INPUT_CONTEXT) {
                (void)iw_key_port_apply_context((iw_input_context_t)event.pin);
                changed = true;
                break;
            } else if ((event.action == IW_INPUT_PRESS || event.action == IW_INPUT_RELEASE) &&
                       (unsigned)event.pin < IW_KEY_COUNT) {
                unsigned key = (unsigned)event.pin;
                if (key_sequences[key] != UINT32_MAX) key_sequences[key]++;
                bool down = event.action == IW_INPUT_PRESS;
                bool feedback = down != recognizer.keys[key].down && !recognizer.keys[key].wait_release;
                unsigned size = iw_keys_edge(&recognizer, key, down, event.tick, signals);
                if (feedback && !recognizer.keys[key].wait_release) callbacks.feedback(key, down);
                if (deliver(signals, size)) { changed = true; break; }
            }
        }
        if ((uint32_t)((uint32_t)rt_tick_get() - started) >= (uint32_t)rt_tick_from_millisecond(2)) break;
    }
    /* 先采样 GPIO，再在短临界区确认队列为空并取得时刻；后到边沿不会被旧采样跨过。 */
    bool pressed[IW_KEY_COUNT] = {false, false};
    for (unsigned key = 0; key < IW_KEY_COUNT; key++)
        if (handles[key] >= 0) pressed[key] = button_is_pressed(handles[key]);
    rt_base_t level = rt_hw_interrupt_disable();
    bool pending = input_queue.count || input_queue.cancel_pending;
    uint32_t now = (uint32_t)rt_tick_get();
    rt_hw_interrupt_enable(level);
    if (!pending && !changed) {
        for (unsigned key = 0; key < IW_KEY_COUNT; key++) iw_keys_recover(&recognizer, key, pressed[key], now);
        iw_key_signal_t signals[IW_KEY_OUTPUTS];
        changed = deliver(signals, iw_keys_advance(&recognizer, now, signals));
        if (!changed) {
            int32_t steps = iw_keys_take_rotation(&recognizer, callbacks.touch_down());
            if (steps && context == IW_INPUT_NORMAL) callbacks.rotate(steps);
        }
    }
    if (pending || changed) iw_gui_wake(IW_GUI_WAKE_INPUT);
}

uint32_t iw_key_port_wait_ms(uint32_t other_ms)
{
    if (!ready) return other_ms;
    RT_ASSERT(rt_thread_self() == owner);
    uint32_t ticks = iw_keys_wait(&recognizer, (uint32_t)rt_tick_get());
    if (ticks == UINT32_MAX) return other_ms;
    uint64_t ms = ((uint64_t)ticks * 1000u + RT_TICK_PER_SECOND - 1u) / RT_TICK_PER_SECOND;
    return ms < other_ms ? (uint32_t)ms : other_ms;
}

bool iw_key_port_apply_context(iw_input_context_t value)
{
    if (!ready || (unsigned)value > IW_INPUT_RECOVERY) return false;
    RT_ASSERT(rt_thread_self() == owner);
    context = value;
    iw_keys_water_lock(&recognizer, context == IW_INPUT_WATER);
    iw_key_port_cancel();
    return true;
}

iw_input_context_t iw_key_port_context(void) { return context; }
bool iw_key_port_inject(unsigned key, bool pressed)
{
    return key < IW_KEY_COUNT && post(pressed ? IW_INPUT_PRESS : IW_INPUT_RELEASE, (int32_t)key);
}
bool iw_key_port_rotate(int32_t steps)
{
    /* 线程侧先过滤拖动期间的注入，GUI 消费时再核对，避免松手后重放旧增量。 */
    if (!ready || callbacks.touch_down()) return false;
    return post(IW_INPUT_ROTATE, steps);
}
bool iw_key_port_set_context(iw_input_context_t value)
{
    return (unsigned)value <= IW_INPUT_RECOVERY && post(IW_INPUT_CONTEXT, (int32_t)value);
}
static void iw_input_stat(void)
{
    iw_input_stats_t stats;
    uint32_t pending;
    uint32_t latency_count;
    uint32_t latency_p95_ms;
    uint32_t latency_max_ms;
    bool latency_p95_at_or_above_limit;
    rt_base_t level = rt_hw_interrupt_disable();
    stats = input_queue.stats;
    pending = input_queue.count;
    latency_count = input_latency_count;
    latency_p95_ms = input_latency_p95(&latency_p95_at_or_above_limit);
    latency_max_ms = input_latency_max_ms;
    rt_hw_interrupt_enable(level);
    rt_kprintf("input accepted=%u consumed=%u rejected=%u discarded=%u "
               "cancel=%u high=%u pending=%u\n",
               (unsigned)stats.accepted, (unsigned)stats.consumed,
               (unsigned)stats.rejected, (unsigned)stats.discarded,
               (unsigned)stats.cancellations, (unsigned)stats.high_water,
               (unsigned)pending);
    rt_kprintf("input latency count=%u p95_ms=%u ge_100=%u max_ms=%u\n",
               (unsigned)latency_count,
               (unsigned)latency_p95_ms, latency_p95_at_or_above_limit ? 1u : 0u,
               (unsigned)latency_max_ms);
}
MSH_CMD_EXPORT(iw_input_stat, Show bounded input queue statistics);

static void iw_input_stat_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_memset(&input_queue.stats, 0, sizeof(input_queue.stats));
    rt_memset(input_latency_histogram, 0, sizeof(input_latency_histogram));
    input_latency_count = 0;
    input_latency_max_ms = 0;
    rt_hw_interrupt_enable(level);
    rt_kprintf("input statistics reset\n");
}
MSH_CMD_EXPORT(iw_input_stat_reset, Reset bounded input queue statistics);


static bool number(const char *text, int32_t *value)
{
    if (!text || !*text) return false;
    bool negative = *text == '-';
    if (negative) text++;
    if (!*text) return false;
    uint32_t limit = negative ? (uint32_t)INT32_MAX + 1u : INT32_MAX;
    uint32_t result = 0;
    for (; *text; text++) {
        if (*text < '0' || *text > '9' || result > (limit - (unsigned)(*text - '0')) / 10u) return false;
        result = result * 10u + (unsigned)(*text - '0');
    }
    *value = negative ? (result == (uint32_t)INT32_MAX + 1u ? INT32_MIN : -(int32_t)result) : (int32_t)result;
    return true;
}

static void iw_key(int argc, char **argv)
{
    int32_t value;
    bool accepted = false;
    if (argc == 4 && !strcmp(argv[1], "edge") && number(argv[2], &value) && value >= 0 && value < IW_KEY_COUNT &&
        (!strcmp(argv[3], "down") || !strcmp(argv[3], "up")))
        accepted = iw_key_port_inject((unsigned)value, !strcmp(argv[3], "down"));
    else if (argc == 3 && !strcmp(argv[1], "rotate") && number(argv[2], &value))
        accepted = iw_key_port_rotate(value);
    else if (argc == 3 && !strcmp(argv[1], "context") && number(argv[2], &value) && value >= 0 && value <= IW_INPUT_RECOVERY)
        accepted = iw_key_port_set_context((iw_input_context_t)value);
    else { rt_kprintf("iw_key edge <0|1> <down|up> | rotate <steps> | context <0..6>\n"); return; }
    rt_kprintf("key queued=%u source=software\n", accepted);
}
MSH_CMD_EXPORT(iw_key, D09 bounded semantic input diagnostics);
