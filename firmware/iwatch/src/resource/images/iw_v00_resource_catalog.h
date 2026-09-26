/* 本文件由 build_v00_resource_bridge.py 生成，不要手改。 */
#ifndef IW_V00_RESOURCE_CATALOG_H
#define IW_V00_RESOURCE_CATALOG_H

#include "../iw_v00_resource_guard.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>

size_t iw_v00_resource_catalog_count(void);
const lv_image_dsc_t *iw_v00_resource_catalog_image(size_t index);
iw_v00_resource_result_t iw_v00_resource_catalog_validate(size_t index, bool for_release);

#endif
