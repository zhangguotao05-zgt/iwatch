/* 固定 SDK 的实际寄存器读取和 FT6146 解码；仅 I2C、IRQ 与任务调度使用窄替身。 */
#include <setjmp.h>
typedef int8_t rt_int8_t;
typedef uint32_t rt_tick_t;
#define RT_EEMPTY 4
#define RT_ERROR 1
#define RT_I2C_WR 0
#define RT_I2C_RD 1
#define FT_DEV_ADDR 0x38
#define MAX_POINT_NUM 2
#define CTP_DOWN 0
#define CTP_UP 1
#define CTP_MOVE 2
#define BSP_TOUCH_SAMPLE_HZ 120
#define RT_TICK_PER_SECOND 1000
#define LOG_D(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define LOG_E(...) do { if (0) printf(__VA_ARGS__); } while (0)
struct rt_i2c_msg { unsigned addr, flags; uint8_t *buf; unsigned len; };
static int ft_bus;
static uint8_t abnormal_recovery, point_report_check_en, time_100ms_count;
typedef struct { uint8_t bytes[14]; bool fail; } touch_packet_t;
static const touch_packet_t *packets;
static size_t packet_count, packet_index, observed_index;
static unsigned api_depth, irq_enables, delays;
static void (*after_packet)(size_t);
static jmp_buf sampler_done;
static lv_indev_t *sample_pointer;

static int rt_i2c_transfer(int bus, struct rt_i2c_msg *msgs, unsigned count)
{
    assert(bus == ft_bus && api_depth == 1 && thread_id == 2 && count == 2);
    assert(msgs[0].addr == FT_DEV_ADDR && msgs[0].flags == RT_I2C_WR);
    assert(msgs[0].len == 1 && msgs[0].buf[0] == 0x01);
    assert(msgs[1].addr == FT_DEV_ADDR && msgs[1].flags == RT_I2C_RD && msgs[1].len == 14);
    assert(packet_index < packet_count);
    const touch_packet_t *packet = &packets[packet_index++];
    if (packet->fail) return 1;
    for (unsigned i = 0; i < 14u; ++i) msgs[1].buf[i] = packet->bytes[i];
    return 2;
}
static void rt_touch_irq_pin_enable(unsigned enabled)
{
    assert(enabled == 1 && thread_id == 2); ++irq_enables;
}
#ifdef _MSC_VER
#pragma warning(push)
/* 保留原 SDK 未使用局部量及线程参数，不改被测函数以适配主机。 */
#pragma warning(disable: 4100 4189)
#endif
#include "ft6146_regs_under_test.inc"
#include "ft6146_point_under_test.inc"

struct sampler_ops { rt_err_t (*read_point)(touch_msg_t); };
struct sampler_driver { int isr_sem; struct sampler_ops *ops; };
static struct sampler_ops sample_ops = {read_point};
static struct sampler_driver sample_driver = {1, &sample_ops};
static struct sampler_driver *current_driver = &sample_driver;
static rt_err_t tp_init(void) { return RT_EOK; }
static void touch_api_lock(void) { assert(thread_id == 2 && !api_depth); ++api_depth; }
static void touch_api_unlock(void) { assert(api_depth == 1); --api_depth; }
static rt_tick_t rt_tick_get(void) { return (rt_tick_t)packet_index; }
static void rt_thread_delay(unsigned ticks)
{
    assert(ticks == RT_TICK_PER_SECOND / BSP_TOUCH_SAMPLE_HZ && !api_depth); ++delays;
}
static rt_err_t rt_sem_take(int sem, int timeout)
{
    assert(sem == 1 && timeout == RT_WAITING_FOREVER && !api_depth && !mutex_depth);
    if (packet_index != observed_index) {
        observed_index = packet_index;
        thread_id = 1;
        if (after_packet) after_packet(packet_index);
        thread_id = 2;
    }
    if (packet_index == packet_count) longjmp(sampler_done, 1);
    return RT_EOK;
}
#include "touch_sampler_under_test.inc"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

static touch_packet_t touch_packet(unsigned points, unsigned event)
{
    /* 按钮内部 (150,90)，其余字节为零；真实 SDK 决定 DOWN/UP/无输出。 */
    touch_packet_t packet = {{0}, false};
    packet.bytes[1] = (uint8_t)points;
    packet.bytes[2] = (uint8_t)(event << 6);
    packet.bytes[3] = 150;
    packet.bytes[5] = 90;
    return packet;
}
static void sample_packets(const touch_packet_t *input, size_t count, void (*observe)(size_t))
{
    assert(thread_id == 1 && count && !api_depth && !mutex_depth);
    packets = input; packet_count = count; packet_index = observed_index = 0;
    after_packet = observe; irq_enables = delays = 0;
    thread_id = 2;
    /* 只在下一次阻塞等待处结束有限脚本；不改变生产读取循环或消息寿命。 */
    if (!setjmp(sampler_done)) tp_read_thread_entry(NULL);
    thread_id = 1;
    assert(packet_index == count && irq_enables == count && delays <= count);
    assert(!api_depth && !mutex_depth);
}
static void sample_one(touch_packet_t packet)
{
    sample_packets(&packet, 1, NULL);
}
static void inject_invalid_read(void)
{
    const touch_packet_t failure = {{0}, true};
    sample_one(failure);
}
/* 窄回调契约替身只改变“还有点”的返回码，解码仍执行实际 FT6146。 */
static rt_err_t continuation_read(touch_msg_t msg)
{
    rt_err_t result = read_point(msg);
    return packet_index < packet_count ? RT_EOK : result;
}
static void assert_isolated(void)
{
    iw_touch_stats_t stats = snapshot();
    assert(stats.wait_release && !stats.queued && !actions);
}
static void recover_and_click(void)
{
    /* 无效之后 DOWN 不能放行；只有新的可信 UP 被 GUI 消费后才允许下一次。 */
    sample_one(touch_packet(1, CTP_DOWN)); pointer_read(sample_pointer);
    assert_isolated();
    sample_one(touch_packet(0, CTP_UP)); pointer_read(sample_pointer);
    assert(!snapshot().wait_release && !actions);
    sample_one(touch_packet(1, CTP_DOWN)); pointer_read(sample_pointer);
    sample_one(touch_packet(0, CTP_UP)); pointer_read(sample_pointer);
    assert(actions == 1);
}
static void observe_failure(size_t index)
{
    if (index == 1) {
        pointer_read(sample_pointer);
        assert(presses == 1 && !actions);
    } else {
        assert_isolated();
        if (index == 2) assert(snapshot().cancel_pending);
        pointer_read(sample_pointer);
        assert_isolated();
        assert(!snapshot().cancel_pending && resets == 1);
    }
}
int test_touch_invalid(size_t mode)
{
    assert(mode <= 5);
    test_font_owner(true, true);
    assert(iw_key_feedback_init());
    sample_pointer = lv_indev_create();
    lv_indev_set_type(sample_pointer, LV_INDEV_TYPE_POINTER);
    assert(iw_touch_input_init());
    reset_queue(); touch_context = IW_INPUT_NORMAL;
    actions = presses = resets = 0;
    iw_component_t frame = {0}, button = {0};
    create_page(&frame, &button);
    const touch_packet_t failure = {{0}, true};
    if (mode == 0) {
        /* 旧版反证先真实 DOWN 初始化 msg，再失败；不执行首次未初始化 UB。 */
        touch_packet_t sequence[] = {touch_packet(1, CTP_DOWN), failure, failure, failure};
        unsigned before_wakes = wakes;
        sample_packets(sequence, 4, observe_failure);
        assert(wakes == before_wakes + 2 && !lv_obj_has_state(button.object, LV_STATE_PRESSED));
        recover_and_click();
    } else if (mode == 1) {
        /* 仅修正版执行首次读失败；检查启动时默认 UP 不能作为恢复证据。 */
        sample_one(failure); assert_isolated();
        assert(iw_touch_input_service()); pointer_read(sample_pointer);
        assert_isolated();
        unsigned before_wakes = wakes;
        for (unsigned i = 0; i < 16; ++i) sample_one(failure);
        assert(wakes == before_wakes && !snapshot().cancel_pending);
        recover_and_click();
    } else if (mode == 2) {
        /* 同一线程内先合法样本，再非法点数/保留事件；不允许复用旧 msg。 */
        touch_packet_t sequence[] = {touch_packet(1, CTP_DOWN), touch_packet(3, CTP_DOWN)};
        sample_packets(sequence, 2, observe_failure);
        sample_one(touch_packet(0, CTP_UP));
        sample_one(touch_packet(1, CTP_DOWN));
        assert(iw_touch_input_service() == false); pointer_read(sample_pointer);
        assert_isolated();
        sample_one(touch_packet(0, CTP_UP));
        sample_one(touch_packet(1, 3));
        pointer_read(sample_pointer); assert_isolated();
        recover_and_click();
    } else if (mode == 3) {
        /* GUI 尚未消费取消时，UP→DOWN 及 UP→再失败均不能复用旧释放。 */
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            sample_one(failure);
            sample_one(touch_packet(0, CTP_UP));
            sample_one(repeat ? failure : touch_packet(1, CTP_DOWN));
            assert(snapshot().cancel_pending);
            assert(iw_touch_input_service()); pointer_read(sample_pointer);
            assert_isolated();
            sample_one(touch_packet(0, CTP_UP)); pointer_read(sample_pointer);
            assert(!snapshot().wait_release && !actions);
        }
        /* 锁取消与采样失效共用隔离协议；业务锁仍使用生产输入入口。 */
        touch_context = IW_INPUT_WATER; input_cancel_lvgl();
        sample_one(failure);
        sample_one(touch_packet(0, CTP_UP)); pointer_read(sample_pointer);
        sample_one(touch_packet(1, CTP_DOWN)); pointer_read(sample_pointer);
        assert(!actions && !presses);
        touch_context = IW_INPUT_NORMAL; input_cancel_lvgl();
        recover_and_click();
    } else if (mode == 4) {
        sample_one(touch_packet(1, CTP_DOWN)); pointer_read(sample_pointer);
        assert(presses == 1 && !actions);
        sample_one(touch_packet(0, CTP_UP));
        /* 已取到真实 UP、尚未分发时发生 I2C 失败，读取回调必须先取消。 */
        inject_invalid_after_read = true; pointer_read(sample_pointer);
        assert(!inject_invalid_after_read && resets == 1);
        assert_isolated();
        assert(!lv_obj_has_state(button.object, LV_STATE_PRESSED));
        recover_and_click();
        /* 第二次在主循环服务取消；不是只覆盖直接读取时的取消。 */
        actions = presses = resets = 0;
        sample_one(touch_packet(1, CTP_DOWN)); pointer_read(sample_pointer);
        sample_one(failure);
        assert(iw_touch_input_service()); pointer_read(sample_pointer);
        assert(resets == 1); assert_isolated();
        recover_and_click();
    } else {
        sample_ops.read_point = continuation_read;
        touch_packet_t valid[] = {touch_packet(1, CTP_DOWN), touch_packet(1, CTP_MOVE), touch_packet(0, CTP_UP)};
        sample_packets(valid, 3, NULL);
        assert(observed_index == 3 && delays == 3 && snapshot().queued == 2);
        pointer_read(sample_pointer); assert(actions == 1);
        actions = presses = resets = 0;
        touch_packet_t invalid[] = {touch_packet(1, CTP_DOWN), failure, failure};
        sample_packets(invalid, 3, NULL);
        assert_isolated();
        assert(snapshot().cancel_pending);
        pointer_read(sample_pointer); assert_isolated();
        sample_ops.read_point = read_point;
        recover_and_click();
    }
    assert(!test_font_assert_count() && !api_depth && !mutex_depth);
    input_cancel_lvgl(); close_page(&frame); lv_indev_delete(sample_pointer);
    printf("touch_invalid mode=%zu real_ft6146=1 real_sampler_queue_cancel=1 recovered_click=1 result=ok\n", mode);
    return 0;
}
