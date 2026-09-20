#include "iw_recent_apps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t captured, released;
static uint32_t capture(iw_route_t route)
{
    assert(route.page_id);
    return ++captured;
}
static void release(uint32_t handle)
{
    assert(handle && handle <= captured);
    released++;
}

int main(void)
{
    static const iw_nav_preview_ops_t ops = {capture, release};
    iw_recent_apps_t recent = {.previews = &ops};
    iw_page_resume_t page = {.route = {IW_PAGE_FACE, 0}};
    assert(!iw_recent_record(&recent, &page) && !recent.count);
    page.route.page_id = IW_PAGE_DIAGNOSTICS;
    assert(!iw_recent_record(&recent, &page) && !recent.count);
    page.route.page_id = IW_PAGE_STOPWATCH;
    page.scroll_y = 40;
    assert(iw_recent_record(&recent, &page));
    assert(recent.count == 1 && iw_recent_get(&recent, 0)->resume.scroll_y == 40);
    page.scroll_y = 55;
    assert(iw_recent_record(&recent, &page));
    assert(recent.count == 1 && iw_recent_get(&recent, 0)->resume.scroll_y == 55);
    assert(captured == 2 && released == 1);

    page = (iw_page_resume_t){.route = {IW_PAGE_ALARM_EDIT, 123}, .draft_bytes = 2,
                              .draft = {4, 5}, .scroll_y = 25};
    assert(iw_recent_record(&recent, &page));
    const iw_recent_entry_t *entry = iw_recent_get(&recent, 1);
    assert(entry && entry->resume.route.page_id == IW_PAGE_ALARM_LIST &&
        !entry->resume.route.argument && !entry->resume.draft_bytes &&
        !entry->resume.scroll_y && !entry->resume.draft[0]);
    page.route.page_id = IW_PAGE_TIMER_DETAIL;
    page.route.argument = 999;
    page.draft_bytes = 0;
    assert(iw_recent_record(&recent, &page));
    assert(iw_recent_get(&recent, 2)->resume.route.argument == 999);
    page.route.page_id = IW_PAGE_SETTINGS;
    page.route.argument = 0;
    assert(iw_recent_record(&recent, &page));
    assert(recent.count == 4);
    assert(iw_recent_remove(&recent, 0) && recent.count == 3);
    assert(released == 2);
    iw_recent_clear(&recent);
    assert(!recent.count && released == captured);

    /* 六项容量边界用不同 App 身份构造；第七项必须只回收最旧句柄。 */
    for (unsigned i = 0; i < IW_NAV_HISTORY; i++) {
        recent.entries[i].app_id = (uint16_t)(100 + i);
        recent.entries[i].preview_handle = ++captured;
    }
    recent.count = IW_NAV_HISTORY;
    uint32_t old_released = released;
    page = (iw_page_resume_t){.route = {IW_PAGE_STOPWATCH, 0}};
    assert(iw_recent_record(&recent, &page));
    assert(recent.count == IW_NAV_HISTORY && released == old_released + 1 &&
        recent.entries[0].app_id == 101 &&
        recent.entries[IW_NAV_HISTORY - 1].app_id == IW_APP_STOPWATCH);
    iw_recent_clear(&recent);
    assert(released == captured);
    puts("D13-B 最近 App：系统页过滤、草稿降级、去重和第七项驱逐通过");
    return 0;
}
