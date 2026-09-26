#include "iw_v00_resource_catalog.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* 真实生成的前景载荷与固定目录逐项配对；发布许可仍单独拒绝。 */
    assert(iw_v00_resource_catalog_count() == 37u);
    for (size_t i = 0; i < iw_v00_resource_catalog_count(); ++i) {
        assert(iw_v00_resource_catalog_validate(i, false) == IW_V00_RESOURCE_OK);
        assert(iw_v00_resource_catalog_validate(i, true) ==
               IW_V00_RESOURCE_NOT_PUBLISHABLE);
    }
    assert(iw_v00_resource_catalog_validate(iw_v00_resource_catalog_count(), false) ==
           IW_V00_RESOURCE_INVALID);
    puts("V00 resource catalog OK");
    return 0;
}
