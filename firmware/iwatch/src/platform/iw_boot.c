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
