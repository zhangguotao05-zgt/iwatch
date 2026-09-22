#ifndef IW_PRODUCT_CONTROLLER_H
#define IW_PRODUCT_CONTROLLER_H
#include "iw_product_view.h"
typedef struct iw_product_page {
    iw_product_view_t view;
    iw_product_model_t model;
    struct iw_product_page *next;
    void (*navigate)(uint16_t, uint32_t, void *);
    void (*quiesce)(void *);
    void *context;
    uint32_t generation, session, request, last_poll, last_preview, argument;
    uint16_t page_id;
    uint8_t next_level;
    bool queued_level, final_level, visible, dirty, linked, exiting;
} iw_product_page_t;
bool iw_product_create(iw_product_page_t *page, uint16_t id, uint32_t argument,
                       uint32_t generation, bool back,
                       void (*navigate)(uint16_t, uint32_t, void *),
                       void (*quiesce)(void *), void *context);
void iw_product_resume(iw_product_page_t *page, bool visible);
/* 提交表盘后只同步根页模型，不重新创建或激活视图。 */
void iw_product_face_sync_root(iw_product_page_t *page);
bool iw_product_destroy(iw_product_page_t *page);
bool iw_product_back(iw_product_page_t *page);
bool iw_product_rotate(iw_product_page_t *page, int32_t steps);
void iw_product_set_recent(iw_product_page_t *page, const iw_recent_apps_t *recent);
/* GUI owner 更新锁页进度；输入驱动不直接触碰 LVGL。 */
void iw_product_set_lock_progress(uint8_t progress);
/* 启动自检结果控制两种锁入口；失败时入口保持可见但禁止点击。 */
void iw_product_set_lock_available(bool available);
/* 普通通知首期只允许 GUI owner 的具名本机生产者；无生产者时保持空态。 */
bool iw_product_notification_add(iw_notification_source_t source,
                                 const char *text, size_t bytes);
/* 仅供 GUI 线程验收切换；不写入用户设置，也不改变草稿和命令状态。 */
bool iw_product_set_profile(iw_theme_quality_t quality, bool large_text, bool reduced_motion);
/* 全局客户端持续处理离页结果；返回下一次检查期限，单位毫秒。 */
uint32_t iw_product_process(void);
/* 表盘提交采用两阶段事务：根页 ONRESUME 确认后提交，其他路径均丢弃候选。 */
bool iw_product_face_commit_pending(void);
void iw_product_face_cancel_pending(void);
void iw_product_face_rollback_pending(void);
#endif
