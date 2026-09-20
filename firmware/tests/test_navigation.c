#include "iw_scope.h"
#include "iw_navigator.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { iw_scope_t *scope; unsigned id, released, paused, resumed; } resource_t;
static unsigned released[IW_SCOPE_ENTRIES], release_count;

static void release(void *pointer)
{
    resource_t *resource = pointer;
    assert(!iw_scope_accepts(resource->scope, resource->scope->token));
    assert(!iw_scope_remove(resource->scope, pointer));
    iw_scope_stop(resource->scope);
    released[release_count++] = resource->id;
    assert(resource->released++ == 0);
}
static void pause_resource(void *pointer)
{
    resource_t *resource = pointer;
    resource->paused++;
    assert(!iw_scope_remove(resource->scope, pointer));
}
static void resume_resource(void *pointer) { ((resource_t *)pointer)->resumed++; }

static void test_scope(void)
{
    static const unsigned limits[] = {IW_SCOPE_CALLBACKS, IW_SCOPE_TIMERS, IW_SCOPE_ANIMATIONS, IW_SCOPE_HANDLES};
    iw_scope_t scope = {0};
    resource_t resources[IW_SCOPE_ENTRIES + 1];
    assert(!iw_scope_init(&scope, 1, 0));
    for (unsigned round = 1; round <= 1000; round++) {
        memset(resources, 0, sizeof(resources));
        release_count = 0;
        assert(iw_scope_init(&scope, IW_PAGE_DIAGNOSTICS, round));
        assert(!iw_scope_init(&scope, 1, round + 1));
        assert(!iw_scope_accepts(&scope, (iw_page_token_t){IW_PAGE_DIAGNOSTICS, round - 1}));
        unsigned n = 0;
        for (unsigned kind = 0; kind < IW_SCOPE_KIND_COUNT; kind++) {
            for (unsigned i = 0; i < limits[kind]; i++) {
                resources[n].scope = &scope; resources[n].id = n;
                assert(iw_scope_add(&scope, (iw_scope_kind_t)kind, &resources[n], release, pause_resource, resume_resource));
                assert(!iw_scope_add(&scope, (iw_scope_kind_t)kind, &resources[n], release, pause_resource, resume_resource));
                n++;
            }
            assert(!iw_scope_add(&scope, (iw_scope_kind_t)kind, &resources[n], release, pause_resource, resume_resource));
        }
        assert(n == IW_SCOPE_ENTRIES);
        assert(!iw_scope_add(&scope, (iw_scope_kind_t)-1, &resources[n], release, NULL, NULL));
        iw_scope_resume(&scope); iw_scope_resume(&scope);
        iw_scope_pause(&scope); iw_scope_pause(&scope);
        for (unsigned i = 0; i < n; i++) assert(resources[i].resumed == 1 && resources[i].paused == 2);
        iw_scope_stop(&scope); iw_scope_stop(&scope);
        assert(release_count == IW_SCOPE_ENTRIES);
        unsigned offset = 0;
        for (unsigned kind = 0; kind < IW_SCOPE_KIND_COUNT; kind++) {
            for (unsigned i = 0; i < limits[kind]; i++) assert(released[offset + i] == offset + limits[kind] - i - 1);
            offset += limits[kind];
        }
        assert(!scope.count && !scope.visible && !scope.alive);
    }
    /* 提前退还的资源不再由 scope 删除；调用方保留所有权。 */
    assert(iw_scope_init(&scope, 1, 1001));
    resources[0] = (resource_t){&scope, 0, 0, 0, 0};
    assert(!iw_scope_add(&scope, IW_SCOPE_TIMER, &resources[0], release, NULL, NULL));
    assert(iw_scope_add(&scope, IW_SCOPE_HANDLE, &resources[0], release, NULL, NULL));
    assert(iw_scope_remove(&scope, &resources[0]));
    assert(!iw_scope_remove(&scope, &resources[0]));
    iw_scope_stop(&scope);
    assert(!resources[0].released);
}

static iw_route_t diagnostic(uint32_t argument) { return (iw_route_t){IW_PAGE_DIAGNOSTICS, argument}; }
static uint32_t previews_live, preview_sequence;
static bool preview_oom;
static uint32_t capture_preview(iw_route_t route)
{
    assert(route.page_id);
    if (preview_oom) return 0;
    previews_live++;
    return ++preview_sequence;
}
static void release_preview(uint32_t handle) { assert(handle && previews_live); previews_live--; }
static void finish(iw_navigator_t *nav, bool reversed)
{
    uint32_t sequence = nav->sequence;
    iw_nav_prepared(nav, sequence);
    iw_nav_resumed(nav, sequence - 1, nav->candidate);
    assert(!nav->resumed);
    if (reversed) {
        iw_nav_finished(nav, sequence, true);
        assert(nav->state == IW_NAV_TRANSITIONING);
        iw_nav_resumed(nav, sequence, nav->candidate);
    } else {
        iw_nav_resumed(nav, sequence, nav->candidate);
        assert(nav->state == IW_NAV_TRANSITIONING);
        iw_nav_finished(nav, sequence, true);
    }
    assert(nav->state == IW_NAV_COMMITTED);
    uint32_t committed = nav->committed;
    uint8_t count = nav->history_count;
    iw_nav_resumed(nav, sequence, nav->candidate);
    iw_nav_finished(nav, sequence, true);
    iw_nav_finished(nav, sequence, false);
    assert(nav->committed == committed && nav->history_count == count);
    assert(iw_nav_settle(nav));
    assert(!iw_nav_settle(nav));
}

static void test_navigator(void)
{
    iw_navigator_t nav = {0};
    static const iw_nav_preview_ops_t preview_ops = {capture_preview, release_preview};
    assert(iw_nav_set_previews(&nav, &preview_ops));
    iw_nav_observation_t actual = {.current = {IW_PAGE_LAUNCHER_GRID, 0}, .running_apps = 1, .depth = 1};
    assert(iw_nav_begin(&nav, IW_NAV_PUSH, diagnostic(1), &actual, 0) == IW_NAV_UNAVAILABLE);
    actual.busy = true;
    assert(iw_nav_begin(&nav, IW_NAV_PUSH, diagnostic(1), &actual, 7) == IW_NAV_BUSY);
    actual.busy = false;
    assert(iw_nav_begin(&nav, IW_NAV_PUSH, (iw_route_t){IW_PAGE_LOCALE, 0}, &actual, 7) == IW_NAV_UNAVAILABLE);
    assert(iw_nav_begin(&nav, IW_NAV_PUSH, (iw_route_t){IW_PAGE_CONTROL_CENTER, 0}, &actual, 7) == IW_NAV_OK);
    iw_nav_abort(&nav, nav.sequence);
    assert(iw_nav_settle(&nav));
    assert(iw_nav_begin(&nav, IW_NAV_PUSH, (iw_route_t){0xffff, 0}, &actual, 7) == IW_NAV_INVALID);
    for (unsigned i = 0; i < 1000; i++) {
        preview_oom = (i & 1) != 0;
        actual.current = i ? nav.current : actual.current;
        actual.back = (iw_route_t){IW_PAGE_LAUNCHER_GRID, 0};
        assert(iw_nav_begin(&nav, IW_NAV_PUSH, diagnostic(i + 1), &actual, 7) == IW_NAV_OK);
        iw_page_resume_t resume = {.route = actual.current, .scroll_y = (int32_t)i, .selected_id = i};
        assert(iw_nav_save_leaving(&nav, &resume));
        assert(iw_nav_begin(&nav, IW_NAV_PUSH, diagnostic(i + 2), &actual, 7) == IW_NAV_BUSY);
        for (unsigned repeat = 0; repeat < 20; repeat++)
            assert(iw_nav_begin(&nav, IW_NAV_BACK, diagnostic(0), &actual, 7) == IW_NAV_COALESCED);
        assert(!iw_nav_take_back(&nav));
        assert(iw_route_equal(nav.current, actual.current));
        finish(&nav, (i & 1) != 0);
        iw_nav_history_tile_t tile;
        assert(iw_nav_history_tile(&nav, nav.history_count - 1u, &tile));
        assert((tile.preview_handle == 0) == preview_oom && tile.name && tile.icon_id == actual.current.page_id);
        assert(previews_live <= IW_NAV_HISTORY);
        assert(iw_nav_take_back(&nav));
        assert(!iw_nav_take_back(&nav));
        assert(iw_nav_resume_find(&nav, actual.current)->scroll_y == (int32_t)i);
        assert(nav.history_count <= IW_NAV_HISTORY);
    }
    assert(nav.committed == 1000 && nav.history_count == 2);
    assert(!iw_nav_set_previews(&nav, NULL));
    assert(!iw_nav_resume_find(&nav, diagnostic(1)));
    static const uint16_t apps[] = {IW_PAGE_FACE, IW_PAGE_SETTINGS, IW_PAGE_TIMER_LIST,
        IW_PAGE_STOPWATCH, IW_PAGE_ALARM_LIST, IW_PAGE_ACTIVITY, IW_PAGE_WORKOUT_TYPES};
    assert(iw_nav_clear_history(&nav) && !previews_live);
    for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        actual.current = (iw_route_t){apps[i], 0};
        assert(iw_nav_begin(&nav, IW_NAV_PUSH, diagnostic(3000 + i), &actual, 7) == IW_NAV_OK);
        finish(&nav, false);
    }
    assert(nav.history_count == 6 && !iw_nav_resume_find(&nav, (iw_route_t){IW_PAGE_FACE, 0}));

    /* 最深八级、第九级、第三个 App 和摘要第七项分别有界。 */
    actual.current = nav.current; actual.depth = 7;
    assert(iw_nav_begin(&nav, IW_NAV_PUSH, diagnostic(1001), &actual, 7) == IW_NAV_OK);
    finish(&nav, false);
    actual.current = nav.current; actual.depth = 8;
    assert(iw_nav_begin(&nav, IW_NAV_PUSH, diagnostic(1002), &actual, 7) == IW_NAV_CAPACITY);
    actual.new_app = true; actual.running_apps = 2;
    assert(iw_nav_begin(&nav, IW_NAV_PUSH, diagnostic(1002), &actual, 7) == IW_NAV_CAPACITY);
    actual.new_app = false;
    assert(iw_nav_begin(&nav, IW_NAV_BACK, diagnostic(0), &actual, 7) == IW_NAV_OK);
    finish(&nav, true);
    assert(nav.current.page_id == IW_PAGE_LAUNCHER_GRID);

    /* 分配/受理/目标恢复任一阶段失败均不提交历史；迟到成功不能覆盖失败。 */
    actual.current = nav.current; actual.depth = 1;
    for (unsigned stage = 0; stage < 3; stage++) {
        iw_page_resume_t history[IW_NAV_HISTORY];
        memcpy(history, nav.history, sizeof(history));
        assert(iw_nav_begin(&nav, IW_NAV_PUSH, diagnostic(2000), &actual, 7) == IW_NAV_OK);
        if (stage) iw_nav_prepared(&nav, nav.sequence);
        if (stage == 2) iw_nav_resumed(&nav, nav.sequence, nav.candidate);
        iw_nav_abort(&nav, nav.sequence);
        iw_nav_finished(&nav, nav.sequence, true);
        assert(nav.state == IW_NAV_ABORTED && iw_route_equal(nav.current, actual.current));
        assert(!memcmp(history, nav.history, sizeof(history)));
        assert(iw_nav_settle(&nav));
    }
    nav.sequence = UINT32_MAX;
    iw_navigator_t before = nav;
    assert(iw_nav_begin(&nav, IW_NAV_PUSH, diagnostic(1), &actual, 7) == IW_NAV_EXHAUSTED);
    assert(!memcmp(&before, &nav, sizeof(nav)));
    assert(iw_nav_clear_history(&nav) && !previews_live);
}

int main(void)
{
    for (size_t i = 0; i < iw_route_count(); i++) {
        const iw_route_descriptor_t *entry = iw_route_at(i);
        assert(entry && iw_route_find(entry->page_id) == entry && entry->name);
        assert(entry->reference_id);
        if (entry->support == IW_ROUTE_READY) assert(entry->supported_states && entry->test_ids && entry->resource_bundle);
        if (entry->page_id == IW_PAGE_TIMER_DETAIL) assert(entry->entry_params == IW_ROUTE_PARAMS_ID);
        if (i) assert(iw_route_at(i - 1)->page_id < entry->page_id);
        if (entry->parent_id) assert(iw_route_find(entry->parent_id));
    }
    assert(!iw_route_at(iw_route_count()));
    test_scope();
    test_navigator();
    puts("D08 core: registry, 1000 scope cycles, 1000 navigation transactions, T13/T14 and failure boundaries passed");
    return 0;
}
