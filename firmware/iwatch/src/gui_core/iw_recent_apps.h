#ifndef IW_RECENT_APPS_H
#define IW_RECENT_APPS_H

#include "iw_navigator.h"

typedef struct {
    iw_page_resume_t resume;
    uint32_t preview_handle;
    uint16_t app_id;
} iw_recent_entry_t;
typedef struct {
    iw_recent_entry_t entries[IW_NAV_HISTORY];
    const iw_nav_preview_ops_t *previews;
    uint8_t count;
} iw_recent_apps_t;

/* 仅在已确认离开前台的 GUI 安全点调用；不记录系统覆盖页。 */
bool iw_recent_record(iw_recent_apps_t *recent, const iw_page_resume_t *resume);
bool iw_recent_remove(iw_recent_apps_t *recent, unsigned index);
void iw_recent_clear(iw_recent_apps_t *recent);
const iw_recent_entry_t *iw_recent_get(const iw_recent_apps_t *recent, unsigned index);

#endif
