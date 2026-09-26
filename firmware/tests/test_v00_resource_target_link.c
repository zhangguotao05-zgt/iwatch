#include "iw_v00_resource_catalog.h"
#include <stdint.h>

volatile uint32_t iw_v00_link_probe_result;

/* 只用于目标工具链最小链接，让 37 项资源和目录实际进入链接图。 */
void _start(void)
{
    uint32_t result = 0;
    for (size_t i = 0; i < iw_v00_resource_catalog_count(); ++i)
        result += (uint32_t)iw_v00_resource_catalog_validate(i, false);
    iw_v00_link_probe_result = result;
    for (;;) { }
}
