#ifndef IW_PRODUCT_SCENE_H
#define IW_PRODUCT_SCENE_H
#include "iw_settings_model.h"
#include "iw_service.h"
#include "iw_routes.h"
#include "iw_product_text.h"
#include "iw_theme.h"
#include "iw_notifications.h"
#include "iw_recent_apps.h"

#define IW_PRODUCT_NODES 48u
#define IW_PRODUCT_TEXT_BYTES 128u
enum {
    IW_ICON_NONE,
    IW_ICON_BACK,
    IW_ICON_NEXT,
    IW_ICON_SUN,
    IW_ICON_CLOCK,
    IW_ICON_INFO,
    IW_ICON_SETTINGS,
    IW_ICON_TIMER,
    IW_ICON_STOPWATCH,
    IW_ICON_ALARM
};
enum {
    IW_ACTION_BACK = 0x1000,
    IW_ACTION_FIELD,
    IW_ACTION_PICK_PREV = 0x1010,
    IW_ACTION_PICK_NEXT,
    IW_ACTION_PICK_CANCEL,
    IW_ACTION_PICK_CHOOSE,
    IW_ACTION_TIME_CANCEL,
    IW_ACTION_TIME_SAVE,
    IW_ACTION_DIM,
    IW_ACTION_BRIGHTEN,
    IW_ACTION_TRACK,
    IW_ACTION_RELOAD,
    IW_ACTION_PICK_STEP,
    IW_ACTION_TIMER_PRESET_1M = 0x1100,
    IW_ACTION_TIMER_PRESET_3M,
    IW_ACTION_TIMER_PRESET_5M,
    IW_ACTION_TIMER_PRESET_10M,
    IW_ACTION_TIMER_OPEN_BASE = 0x1120,
    IW_ACTION_TIMER_PAUSE = 0x1140,
    IW_ACTION_TIMER_RESUME,
    IW_ACTION_TIMER_CANCEL,
    IW_ACTION_TIMER_RESTART,
    IW_ACTION_STOPWATCH_PRIMARY = 0x1160,
    IW_ACTION_STOPWATCH_LAP,
    IW_ACTION_STOPWATCH_RESET,
    IW_ACTION_ALARM_ADD = 0x1180,
    IW_ACTION_ALARM_OPEN_BASE = 0x11a0,
    IW_ACTION_ALARM_HOUR_MINUS = 0x11c0,
    IW_ACTION_ALARM_HOUR_PLUS,
    IW_ACTION_ALARM_MINUTE_MINUS,
    IW_ACTION_ALARM_MINUTE_PLUS,
    IW_ACTION_ALARM_WEEKDAY_BASE = 0x11d0,
    IW_ACTION_ALARM_ENABLE = 0x11e0,
    IW_ACTION_ALARM_SAVE,
    IW_ACTION_ALARM_DELETE,
    IW_ACTION_ALERT_ACK,
    IW_ACTION_ALERT_SNOOZE,
    IW_ACTION_ALERT_OPEN,
    IW_ACTION_STACK_TIMER,
    IW_ACTION_STACK_ALARM,
    IW_ACTION_NOTIFICATION_DELETE,
    IW_ACTION_FACE_NOTIFICATIONS,
    IW_ACTION_FACE_STACK,
    IW_ACTION_NOTIFICATION_OPEN_BASE = 0x1200,
    IW_ACTION_RECENT_OPEN_BASE = 0x1240,
    IW_ACTION_RECENT_REMOVE_BASE = 0x1280
};
typedef struct {
    int16_t x, y, width, height, baseline;
    uint16_t action;
    uint8_t font_px, radius, align, icon;
    bool fixed, disabled, multiline, picker_item;
    uint32_t color, fill;
    char text[IW_PRODUCT_TEXT_BYTES];
} iw_product_node_t;
typedef struct {
    iw_product_node_t nodes[IW_PRODUCT_NODES];
    uint16_t page_id, count, content_height, clip_top, clip_bottom;
    iw_time_field_t picker_field;
    int32_t picker_value;
} iw_product_scene_t;
typedef struct {
    uint32_t revision;
    uint16_t lap_count;
    uint8_t state;
    uint8_t visible_laps;
    uint64_t elapsed_ms;
    iw_stopwatch_lap_t laps[8];
} iw_stopwatch_view_model_t;
typedef struct {
    iw_clock_snapshot_t clock;
    iw_brightness_snapshot_t brightness;
    iw_timer_snapshot_t timers;
    iw_timer_view_t selected_timer;
    iw_alarm_snapshot_t alarms;
    iw_alarm_edit_t alarm_edit;
    iw_alert_snapshot_t alerts;
    iw_alert_record_t selected_alert;
    iw_stopwatch_view_model_t stopwatch;
    iw_time_draft_t draft;
    const iw_notification_store_t *notifications;
    const iw_recent_apps_t *recent_apps;
    iw_notification_t selected_notification;
    uint32_t notification_ids[IW_NOTIFICATION_CAPACITY];
    const char *hardware, *firmware, *toolchain;
    iw_product_text_id_t message;
    uint8_t preview_level;
    iw_theme_quality_t quality;
    bool back, large_text, pending, display_available, time_available, reduced_motion;
    bool selected_notification_valid;
} iw_product_model_t;
/* 固定容量；全部字符串复制。输出不引用页面草稿或临时格式化缓冲。 */
bool iw_product_scene_build(iw_product_scene_t *scene, uint16_t page_id, const iw_product_model_t *model);
int iw_product_scene_hit(const iw_product_scene_t *scene, int x, int y, int scroll_y);
int iw_product_scene_scroll_limit(const iw_product_scene_t *scene);
#endif
