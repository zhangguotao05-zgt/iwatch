#ifndef IW_SCOPE_H
#define IW_SCOPE_H

#include <stdbool.h>
#include <stdint.h>

enum { IW_SCOPE_TIMERS = 8, IW_SCOPE_ANIMATIONS = 8,
       IW_SCOPE_CALLBACKS = 16, IW_SCOPE_HANDLES = 16, IW_SCOPE_ENTRIES = 48 };

typedef enum { IW_SCOPE_CALLBACK, IW_SCOPE_TIMER, IW_SCOPE_ANIMATION, IW_SCOPE_HANDLE,
               IW_SCOPE_KIND_COUNT } iw_scope_kind_t;
typedef struct { uint16_t page_id; uint32_t generation; } iw_page_token_t;
typedef void (*iw_scope_action_t)(void *resource);
typedef struct {
    void *resource;
    iw_scope_action_t release, pause, resume;
    uint8_t kind;
} iw_scope_entry_t;
typedef struct {
    iw_page_token_t token;
    iw_scope_entry_t entries[IW_SCOPE_ENTRIES];
    uint8_t count, counts[IW_SCOPE_KIND_COUNT];
    bool alive, visible, cleaning;
} iw_scope_t;

/* 初次使用前清零，仅由 GUI owner 使用；generation 单调分配，耗尽后拒绝新建。 */
bool iw_scope_init(iw_scope_t *scope, uint16_t page_id, uint32_t generation);
bool iw_scope_accepts(const iw_scope_t *scope, iw_page_token_t token);
/* 登记失败不接管资源；调用方仍需回收刚创建的资源。timer/动画必须提供暂停和恢复。 */
bool iw_scope_add(iw_scope_t *scope, iw_scope_kind_t kind, void *resource,
                  iw_scope_action_t release, iw_scope_action_t pause, iw_scope_action_t resume);
bool iw_scope_remove(iw_scope_t *scope, void *resource);
void iw_scope_pause(iw_scope_t *scope);
void iw_scope_resume(iw_scope_t *scope);
/* 在绘制资源空闲后调用；先失效令牌，再按回调、timer、动画、句柄顺序逆序回收。 */
void iw_scope_stop(iw_scope_t *scope);

#endif
