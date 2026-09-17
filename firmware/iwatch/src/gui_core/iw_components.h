#ifndef IW_COMPONENTS_H
#define IW_COMPONENTS_H

#include "lvgl.h"
#include "iw_theme.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { IW_SCREEN_FRAME, IW_LIST_ROW, IW_PILL_BUTTON, IW_STATE_PANEL,
               IW_STATUS_BANNER, IW_COMPONENT_KIND_COUNT } iw_component_kind_t;
typedef enum { IW_NORMAL, IW_PRESSED, IW_DISABLED, IW_WAITING, IW_DANGER,
               IW_LOADING, IW_EMPTY, IW_ERROR, IW_UNAVAILABLE,
               IW_INFO, IW_SUCCESS, IW_WARNING, IW_COMPONENT_STATE_COUNT } iw_component_state_t;
typedef enum { IW_FRAME_NORMAL, IW_FRAME_SCROLL, IW_FRAME_FULLSCREEN } iw_frame_mode_t;
typedef enum { IW_COMPONENT_OK, IW_COMPONENT_INVALID, IW_COMPONENT_BUSY,
               IW_COMPONENT_NO_MEMORY, IW_COMPONENT_FONT_ERROR } iw_component_result_t;

/* 句柄地址须保持稳定到父对象销毁；禁止复制，父对象删除时自动清零。 */
typedef struct { lv_obj_t *object; } iw_component_t;
typedef void (*iw_component_action_fn)(uint16_t action, void *context);
typedef struct {
    iw_component_state_t state;
    const char *title, *detail, *value;
} iw_component_view_t;

typedef struct {
    int16_t x, y, width, height;
    iw_theme_quality_t quality;
    iw_theme_font_role_t font_role;
    bool reduced_motion;
    uint16_t action;
    iw_component_action_fn on_action;
    void *context;
} iw_component_config_t;

/* 只在 GUI 线程调用，不在绘制回调中调用。文本复制到固定缓冲，超长/非法 UTF-8 拒绝更新。
 * 标题最多127字节、说明255字节、右值47字节；不截断或保留调用者字符串。
 * ScreenFrame 登记全局故障所有权；其他组件必须创建在它的 content 子树内。
 * quiesce 在删子对象前停止页面定时器/订阅；不得释放句柄所在内存，允许重复调用。
 */
iw_component_result_t iw_screen_frame_create(iw_component_t *handle, lv_obj_t *parent,
    iw_frame_mode_t mode, const char *title, void (*quiesce)(void *), void *context);
lv_obj_t *iw_screen_frame_content(const iw_component_t *handle);
iw_component_result_t iw_component_create(iw_component_t *handle, lv_obj_t *parent,
    iw_component_kind_t kind, const iw_component_config_t *config, const iw_component_view_t *view);
iw_component_result_t iw_component_update(iw_component_t *handle, const iw_component_view_t *view);
/* 正常和故障退出均先等渲染空闲。重复销毁空句柄成功；BUSY 时句柄仍有效。 */
iw_component_result_t iw_component_destroy(iw_component_t *handle);
/* 共用 GUI 主循环推进短按压动画，无私有线程或定时器；true 时建议16ms内再次调用。 */
bool iw_components_tick(void);

#endif
