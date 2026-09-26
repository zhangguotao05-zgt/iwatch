#ifndef IW_V00_SCENE_H
#define IW_V00_SCENE_H

#include "iw_product_scene.h"

/* 六个样片 ID 仅用于主机预览；目标端使用正式页面 ID。 */
enum {
    IW_V00_GRID = 0xa100,
    IW_V00_FACE,
    IW_V00_TIMER,
    IW_V00_ALARM,
    IW_V00_DISPLAY,
    IW_V00_CONTROL,
    IW_V00_COUNT = 6,
    IW_V00_CONTROL_RUNTIME = 0xa110,
    IW_V00_GRID_RUNTIME = 0xa120,
    IW_V00_FACE_RUNTIME,
    IW_V00_TIMER_RUNTIME,
    IW_V00_ALARM_RUNTIME,
    IW_V00_DISPLAY_RUNTIME
};
enum {
    IW_V00_ACTION_TIMER_15M = 0xa200,
    IW_V00_ACTION_TIMER_30M
};

bool iw_v00_scene_build(iw_product_scene_t *scene, uint16_t page_id,
                        const iw_product_model_t *model);
uint16_t iw_v00_route_for_sample(uint16_t page_id);

#endif
