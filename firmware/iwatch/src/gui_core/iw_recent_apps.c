#include "iw_recent_apps.h"
#include <string.h>

static uint16_t app_home(uint16_t app_id)
{
    switch (app_id) {
    case IW_APP_SETTINGS: return IW_PAGE_SETTINGS;
    case IW_APP_TIMER: return IW_PAGE_TIMER_LIST;
    case IW_APP_STOPWATCH: return IW_PAGE_STOPWATCH;
    case IW_APP_ALARM: return IW_PAGE_ALARM_LIST;
    default: return 0;
    }
}

static bool normalize(const iw_page_resume_t *source, iw_recent_entry_t *entry)
{
    const iw_route_descriptor_t *route = iw_route_find(source->route.page_id);
    if (!route || route->support != IW_ROUTE_READY ||
        route->app_id == IW_APP_SYSTEM || route->app_id == IW_APP_DIAGNOSTICS ||
        source->draft_bytes > IW_NAV_DRAFT_BYTES) return false;
    *entry = (iw_recent_entry_t){.resume = *source, .app_id = route->app_id};
    entry->resume.draft_bytes = 0;
    memset(entry->resume.draft, 0, sizeof(entry->resume.draft));
    if (route->resume_policy == IW_RESUME_DRAFT) {
        uint16_t home = app_home(route->app_id);
        if (!home) return false;
        entry->resume.route = (iw_route_t){home, 0};
        entry->resume.scroll_y = 0;
        entry->resume.selected_id = 0;
    }
    return true;
}

bool iw_recent_remove(iw_recent_apps_t *recent, unsigned index)
{
    if (!recent || index >= recent->count || recent->count > IW_NAV_HISTORY) return false;
    if (recent->entries[index].preview_handle && recent->previews)
        recent->previews->release(recent->entries[index].preview_handle);
    memmove(&recent->entries[index], &recent->entries[index + 1],
            (recent->count - index - 1u) * sizeof(recent->entries[0]));
    memset(&recent->entries[--recent->count], 0, sizeof(recent->entries[0]));
    return true;
}

bool iw_recent_record(iw_recent_apps_t *recent, const iw_page_resume_t *resume)
{
    if (!recent || !resume || recent->count > IW_NAV_HISTORY) return false;
    iw_recent_entry_t entry;
    if (!normalize(resume, &entry)) return false;
    for (unsigned i = 0; i < recent->count; i++)
        if (recent->entries[i].app_id == entry.app_id) {
            (void)iw_recent_remove(recent, i);
            break;
        }
    if (recent->count == IW_NAV_HISTORY) (void)iw_recent_remove(recent, 0);
    entry.preview_handle = recent->previews ? recent->previews->capture(entry.resume.route) : 0;
    recent->entries[recent->count++] = entry;
    return true;
}

void iw_recent_clear(iw_recent_apps_t *recent)
{
    if (recent) while (recent->count) (void)iw_recent_remove(recent, recent->count - 1u);
}

const iw_recent_entry_t *iw_recent_get(const iw_recent_apps_t *recent, unsigned index)
{
    return recent && recent->count <= IW_NAV_HISTORY && index < recent->count ?
           &recent->entries[index] : NULL;
}
