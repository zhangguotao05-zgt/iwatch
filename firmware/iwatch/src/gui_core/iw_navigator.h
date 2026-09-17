#ifndef IW_NAVIGATOR_H
#define IW_NAVIGATOR_H

#include "iw_routes.h"

enum { IW_NAV_MAX_APPS = 2, IW_NAV_MAX_DEPTH = 8, IW_NAV_HISTORY = 6, IW_NAV_DRAFT_BYTES = 64 };
typedef enum { IW_NAV_IDLE, IW_NAV_PREPARING, IW_NAV_TRANSITIONING,
               IW_NAV_COMMITTED, IW_NAV_ABORTED } iw_nav_state_t;
typedef enum { IW_NAV_PUSH, IW_NAV_BACK } iw_nav_action_t;
typedef enum { IW_NAV_OK, IW_NAV_COALESCED, IW_NAV_BUSY, IW_NAV_INVALID, IW_NAV_UNAVAILABLE,
               IW_NAV_CAPACITY, IW_NAV_EXHAUSTED, IW_NAV_FAILED } iw_nav_result_t;
typedef struct {
    iw_route_t route;
    int32_t scroll_y;
    uint32_t selected_id;
    uint8_t draft_bytes, draft[IW_NAV_DRAFT_BYTES];
} iw_page_resume_t;
typedef struct {
    iw_route_t current, back;
    uint16_t running_apps, depth;
    bool busy, new_app;
} iw_nav_observation_t;

/* 缩略图生产者在 D13 接入；句柄属于资源服务，禁止传 LVGL 指针。
 * capture 返回 0 表示分配/取图失败，历史仍提交为图标和名称。release 必须无分配。 */
typedef struct {
    uint32_t (*capture)(iw_route_t route);
    void (*release)(uint32_t handle);
} iw_nav_preview_ops_t;
typedef struct { uint32_t preview_handle; uint16_t icon_id; const char *name; } iw_nav_history_tile_t;
typedef struct {
    iw_nav_state_t state;
    iw_nav_action_t action;
    iw_route_t current, candidate;
    iw_page_resume_t leaving, history[IW_NAV_HISTORY];
    uint32_t previews[IW_NAV_HISTORY];
    const iw_nav_preview_ops_t *preview_ops;
    uint32_t sequence, committed, aborted;
    uint8_t history_count;
    bool resumed, finished, pending_back;
} iw_navigator_t;

_Static_assert(sizeof(iw_page_resume_t) <= 128u, "页面恢复摘要不得超过 128 字节");
/* observation 来自真实框架栈，navigator 不维护第二套页面栈。 */
iw_nav_result_t iw_nav_begin(iw_navigator_t *nav, iw_nav_action_t action, iw_route_t target,
                             const iw_nav_observation_t *actual, uint32_t capabilities);
bool iw_nav_save_leaving(iw_navigator_t *nav, const iw_page_resume_t *resume);
void iw_nav_prepared(iw_navigator_t *nav, uint32_t sequence);
void iw_nav_resumed(iw_navigator_t *nav, uint32_t sequence, iw_route_t route);
void iw_nav_finished(iw_navigator_t *nav, uint32_t sequence, bool success);
void iw_nav_abort(iw_navigator_t *nav, uint32_t sequence);
/* 终态只在 GUI 安全点确认；随后才能消费一个合并的返回请求。 */
bool iw_nav_settle(iw_navigator_t *nav);
bool iw_nav_take_back(iw_navigator_t *nav);
const iw_page_resume_t *iw_nav_resume_find(const iw_navigator_t *nav, iw_route_t route);
bool iw_nav_set_previews(iw_navigator_t *nav, const iw_nav_preview_ops_t *ops);
bool iw_nav_history_tile(const iw_navigator_t *nav, unsigned index, iw_nav_history_tile_t *tile);
/* 只在空闲时清理历史；不改动实际运行栈。 */
bool iw_nav_clear_history(iw_navigator_t *nav);

#endif
