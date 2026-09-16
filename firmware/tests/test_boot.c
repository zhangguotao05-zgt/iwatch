#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "iw_boot.h"

static int thread_init_result;
static int gui_init_result;
static int service_init_result;
static int thread_startup_result;
static unsigned thread_init_calls;
static unsigned gui_init_calls;
static unsigned gui_deinit_calls;
static unsigned service_init_calls;
static unsigned thread_startup_calls;
static unsigned thread_detach_calls;

int iw_gui_port_init(void)
{
    gui_init_calls++;
    return gui_init_result;
}

void iw_gui_port_deinit(void)
{
    gui_deinit_calls++;
}

int iw_service_runtime_init(void)
{
    service_init_calls++;
    return service_init_result;
}

int rt_thread_init(struct rt_thread *thread, const char *name, void (*entry)(void *),
                   void *parameter, void *stack_start, rt_uint32_t stack_size,
                   rt_uint8_t priority, rt_uint32_t tick)
{
    (void)thread;
    (void)name;
    (void)entry;
    (void)parameter;
    (void)stack_start;
    (void)stack_size;
    (void)priority;
    (void)tick;
    thread_init_calls++;
    return thread_init_result;
}

int rt_thread_startup(struct rt_thread *thread)
{
    (void)thread;
    thread_startup_calls++;
    return thread_startup_result;
}

int rt_thread_detach(struct rt_thread *thread)
{
    (void)thread;
    thread_detach_calls++;
    return RT_EOK;
}

static void test_entry(void *parameter)
{
    (void)parameter;
}

static int start_once(struct rt_thread *thread)
{
    static uint8_t stack[256];
    return iw_boot_start_app_thread(thread, "test", test_entry, NULL, stack,
                                    sizeof(stack), 10u, 5u);
}

int main(void)
{
    struct rt_thread thread;

    thread_init_result = -5;
    assert(start_once(&thread) == -5);
    assert(thread_init_calls == 1u);
    assert(gui_init_calls == 0u && service_init_calls == 0u);
    assert(thread_startup_calls == 0u && thread_detach_calls == 0u);

    thread_init_result = RT_EOK;
    service_init_result = -7;
    assert(start_once(&thread) == -7);
    assert(gui_init_calls == 1u && service_init_calls == 1u);
    assert(gui_deinit_calls == 1u && thread_detach_calls == 1u);

    service_init_result = RT_EOK;
    thread_startup_result = -8;
    assert(start_once(&thread) == -8);
    assert(gui_init_calls == 2u && service_init_calls == 2u);
    assert(gui_deinit_calls == 1u && thread_detach_calls == 2u);

    thread_startup_result = RT_EOK;
    assert(start_once(&thread) == RT_EOK);
    assert(thread_init_calls == 4u && thread_startup_calls == 2u);
    assert(gui_init_calls == 2u && service_init_calls == 2u);
    assert(gui_deinit_calls == 1u && thread_detach_calls == 2u);

    puts("boot thread failure/retry tests passed");
    return 0;
}
