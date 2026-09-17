#ifndef IW_ROUTES_H
#define IW_ROUTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    IW_PAGE_FACE = 0x0001, IW_PAGE_LAUNCHER_LIST, IW_PAGE_LAUNCHER_GRID,
    IW_PAGE_SETTINGS = 0x0100, IW_PAGE_DISPLAY, IW_PAGE_BRIGHTNESS, IW_PAGE_TIME,
    IW_PAGE_LOCALE, IW_PAGE_ABOUT, IW_PAGE_ACCESSIBILITY,
    IW_PAGE_TIMER_LIST = 0x0200, IW_PAGE_TIMER_CREATE, IW_PAGE_TIMER_DETAIL,
    IW_PAGE_ALERT_TIMER, IW_PAGE_ALERT_ALARM,
    IW_PAGE_STOPWATCH = 0x0300, IW_PAGE_STOPWATCH_LAPS,
    IW_PAGE_ALARM_LIST = 0x0400, IW_PAGE_ALARM_EDIT,
    IW_PAGE_CONTROL_CENTER = 0x0500, IW_PAGE_NOTIFICATION_LIST, IW_PAGE_NOTIFICATION_DETAIL,
    IW_PAGE_SMART_STACK, IW_PAGE_SWITCHER, IW_PAGE_LOCK, IW_PAGE_WATER_LOCK,
    IW_PAGE_FACE_PICKER, IW_PAGE_FACE_EDITOR, IW_PAGE_POWER_SHEET,
    IW_PAGE_ACTIVITY = 0x0600, IW_PAGE_HEART_RATE, IW_PAGE_BLOOD_OXYGEN,
    IW_PAGE_WORKOUT_TYPES, IW_PAGE_WORKOUT_ACTIVE, IW_PAGE_WORKOUT_SUMMARY,
    IW_PAGE_CALENDAR = 0x0700, IW_PAGE_CALCULATOR, IW_PAGE_PHOTO_GRID, IW_PAGE_PHOTO_VIEWER,
    IW_PAGE_MINDFULNESS, IW_PAGE_WEATHER_DEMO, IW_PAGE_WALLET_DEMO, IW_PAGE_COMMUNICATION_DEMO,
    IW_PAGE_COMPONENT_GALLERY = 0x9000, IW_PAGE_DIAGNOSTICS, IW_PAGE_RECOVERY
} iw_page_id_t;
typedef enum { IW_APP_SYSTEM = 1, IW_APP_SETTINGS, IW_APP_TIMER, IW_APP_STOPWATCH,
    IW_APP_ALARM, IW_APP_HEALTH, IW_APP_WORKOUT, IW_APP_CALENDAR, IW_APP_CALCULATOR,
    IW_APP_PHOTOS, IW_APP_MINDFULNESS, IW_APP_OFFLINE, IW_APP_DIAGNOSTICS } iw_app_id_t;
typedef enum { IW_ROUTE_RESERVED, IW_ROUTE_LEGACY, IW_ROUTE_READY, IW_ROUTE_OVERLAY } iw_route_support_t;
typedef enum { IW_RESUME_FRESH, IW_RESUME_POSITION, IW_RESUME_DRAFT } iw_resume_policy_t;
enum { IW_ROUTE_CAP_DISPLAY = 1u, IW_ROUTE_CAP_TIME = 2u, IW_ROUTE_CAP_BRIGHTNESS = 4u };
enum { IW_ROUTE_STATE_CONTENT = 1u, IW_ROUTE_STATE_ERROR = 2u, IW_ROUTE_STATE_UNAVAILABLE = 4u };
typedef enum { IW_ROUTE_PARAMS_RESERVED, IW_ROUTE_PARAMS_NONE, IW_ROUTE_PARAMS_ID } iw_route_params_t;
enum { IW_ROUTE_RESOURCE_NONE, IW_ROUTE_RESOURCE_BUILTIN };
enum { IW_ROUTE_TEST_T13 = 1u, IW_ROUTE_TEST_T14 = 2u };

typedef struct { uint16_t page_id; uint32_t argument; } iw_route_t;
typedef struct {
    uint16_t page_id, app_id, parent_id;
    uint32_t required_capabilities;
    uint8_t support, resume_policy;
    uint16_t supported_states, test_ids;
    uint8_t entry_params, resource_bundle;
    const char *reference_id;
    const char *name;
} iw_route_descriptor_t;

const iw_route_descriptor_t *iw_route_find(uint16_t page_id);
const iw_route_descriptor_t *iw_route_at(size_t index);
size_t iw_route_count(void);
bool iw_route_equal(iw_route_t left, iw_route_t right);

#endif
