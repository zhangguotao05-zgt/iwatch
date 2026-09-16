#include "iw_boot.h"

#include <stdbool.h>
#include <rtthread.h>

#include "iw_gui_port.h"
#include "iw_service_runtime.h"

static bool boot_initialized;

int iw_boot_init(void)
{
    int result;

    if (boot_initialized) return RT_EOK;
    result = iw_gui_port_init();
    if (result != RT_EOK) return result;

    result = iw_service_runtime_init();
    if (result != RT_EOK)
    {
        iw_gui_port_deinit();
        return result;
    }
    boot_initialized = true;
    return RT_EOK;
}

rt_err_t iw_boot_start_app_thread(struct rt_thread *thread,
                                  const char *name,
                                  void (*entry)(void *parameter),
                                  void *parameter,
                                  void *stack_start,
                                  rt_uint32_t stack_size,
                                  rt_uint8_t priority,
                                  rt_uint32_t tick)
{
    rt_err_t result;

    result = rt_thread_init(thread, name, entry, parameter, stack_start, stack_size,
                            priority, tick);
    if (result != RT_EOK) return result;

    result = iw_boot_init();
    if (result != RT_EOK)
    {
        rt_thread_detach(thread);
        return result;
    }

    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        rt_thread_detach(thread);
        /* GUI 事件和服务线程由启动协调者持有，重试时复用，不能由应用线程单独释放。 */
    }
    return result;
}
