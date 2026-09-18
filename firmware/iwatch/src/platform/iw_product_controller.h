#ifndef IW_PRODUCT_CONTROLLER_H
#define IW_PRODUCT_CONTROLLER_H
#include "iw_product_view.h"
typedef struct iw_product_page {
    iw_product_view_t view;
    iw_product_model_t model;
    struct iw_product_page *next;
    void (*navigate)(uint16_t, void *);
    void (*quiesce)(void *);
    void *context;
    uint32_t generation, session, request, last_poll, last_preview;
    uint16_t page_id;
    uint8_t next_level;
    bool queued_level, final_level, visible, dirty, linked, exiting;
} iw_product_page_t;
bool iw_product_create(iw_product_page_t *page, uint16_t id, uint32_t generation, bool back,
                       void (*navigate)(uint16_t, void *), void (*quiesce)(void *), void *context);
void iw_product_resume(iw_product_page_t *page, bool visible);
bool iw_product_destroy(iw_product_page_t *page);
bool iw_product_back(iw_product_page_t *page);
bool iw_product_rotate(iw_product_page_t *page, int32_t steps);
/* 仅供 GUI 线程验收切换；不写入用户设置，也不改变草稿和命令状态。 */
bool iw_product_set_profile(iw_theme_quality_t quality, bool large_text, bool reduced_motion);
/* 全局客户端持续处理离页结果；返回下一次检查期限，单位毫秒。 */
uint32_t iw_product_process(void);
#endif
