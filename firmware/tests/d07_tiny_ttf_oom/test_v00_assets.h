#ifndef TEST_V00_ASSETS_H
#define TEST_V00_ASSETS_H
#include "lvgl.h"

bool test_v00_assets_load(void);
const lv_image_dsc_t *const *test_v00_assets_images(void);
void test_v00_assets_release(void);

#endif
