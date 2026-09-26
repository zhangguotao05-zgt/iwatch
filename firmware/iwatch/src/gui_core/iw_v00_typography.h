#ifndef IW_V00_TYPOGRAPHY_H
#define IW_V00_TYPOGRAPHY_H

#include <stdint.h>

typedef enum {
    IW_V00_TYPE_FACE_DATE,
    IW_V00_TYPE_FACE_WEEKDAY,
    IW_V00_TYPE_FACE_TIME,
    IW_V00_TYPE_FACE_CAPTION,
    IW_V00_TYPE_FACE_QUICK_TIME,
    IW_V00_TYPE_TOP_TIME,
    IW_V00_TYPE_PAGE_TITLE,
    IW_V00_TYPE_TIMER_SECTION,
    IW_V00_TYPE_TIMER_PRESET,
    IW_V00_TYPE_TIMER_UNIT,
    IW_V00_TYPE_ACTION_LABEL,
    IW_V00_TYPE_ALARM_FORMAT,
    IW_V00_TYPE_ALARM_VALUE,
    IW_V00_TYPE_ALARM_SEPARATOR,
    IW_V00_TYPE_SETTINGS_SECTION,
    IW_V00_TYPE_ROW_TITLE,
    IW_V00_TYPE_ROW_DETAIL,
    IW_V00_TYPE_CONTROL_BATTERY,
    IW_V00_TYPE_COUNT
} iw_v00_type_id_t;

typedef struct {
    uint8_t size_px;
    uint16_t weight;
    int8_t tracking_px;
} iw_v00_type_style_t;

/* 字号与字重来自 IW-V00-FONT-VISUAL-20260923-1，其他样式沿用设计规格。 */
const iw_v00_type_style_t *iw_v00_type_style(iw_v00_type_id_t id);

#endif
