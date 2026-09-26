#ifndef IW_SETTINGS_MODEL_H
#define IW_SETTINGS_MODEL_H

#include "iw_time.h"
#include <stddef.h>

typedef struct {
    int16_t year;
    /* 星期一为 0；草稿日期非法时 weekday 为 UINT8_MAX。 */
    uint8_t month, day, hour, minute, second, weekday;
} iw_calendar_fields_t;

typedef enum {
    IW_EDIT_YEAR,
    IW_EDIT_MONTH,
    IW_EDIT_DAY,
    IW_EDIT_HOUR,
    IW_EDIT_MINUTE,
    IW_EDIT_OFFSET,
    IW_EDIT_NONE
} iw_time_field_t;

typedef enum {
    IW_DRAFT_OK,
    IW_DRAFT_INVALID_DATE,
    IW_DRAFT_UTC_RANGE,
    IW_DRAFT_INVALID_ARGUMENT
} iw_draft_status_t;

typedef struct {
    iw_calendar_fields_t value;
    uint32_t expected_revision;
    int16_t offset_minutes, candidate;
    iw_time_field_t editing;
    bool needs_calibration;
} iw_time_draft_t;

/* 无 RTOS、LVGL、堆分配或设备写入；失败时不修改输出。星期一为 0。 */
bool iw_clock_local_fields(const iw_clock_snapshot_t *snapshot, iw_calendar_fields_t *output);
bool iw_time_draft_begin(iw_time_draft_t *draft, const iw_clock_snapshot_t *snapshot);
bool iw_time_draft_select(iw_time_draft_t *draft, iw_time_field_t field);
bool iw_time_draft_step(iw_time_draft_t *draft, int32_t steps);
bool iw_time_draft_choose(iw_time_draft_t *draft);
void iw_time_draft_cancel_field(iw_time_draft_t *draft);
/* 只转换已选定草稿，秒归零；非法日保留给界面提示，不自动修正。 */
iw_draft_status_t iw_time_draft_utc(const iw_time_draft_t *draft, int64_t *utc_seconds);
/* 缓冲不足时返回 false 并清空可写输出；UTC 无效时显示 --:--。 */
bool iw_clock_format_hm(const iw_clock_snapshot_t *snapshot, char *output, size_t capacity);
bool iw_clock_format_offset(int16_t minutes, char *output, size_t capacity);
/* 控制中心整宽轨道使用屏幕坐标 36/354。 */
uint8_t iw_brightness_track_level(int32_t x);
/* 显示页中间轨道使用屏幕坐标 114/276，避开两侧太阳按钮。 */
uint8_t iw_brightness_detail_level(int32_t x);
/* V00 显示页的可见轨道为屏幕坐标 121/269。 */
uint8_t iw_brightness_v00_display_level(int32_t x);
uint8_t iw_brightness_step_level(uint8_t current, int32_t steps);

#endif
