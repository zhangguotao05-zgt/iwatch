#include "iw_navigator.h"
#include <string.h>

static void increment(uint32_t *counter) { if (*counter != UINT32_MAX) (*counter)++; }

iw_nav_result_t iw_nav_begin(iw_navigator_t *nav, iw_nav_action_t action, iw_route_t target,
                             const iw_nav_observation_t *actual, uint32_t capabilities)
{
    if (!nav || !actual || (action != IW_NAV_PUSH && action != IW_NAV_BACK)) return IW_NAV_INVALID;
    if (nav->state != IW_NAV_IDLE) {
        if (action != IW_NAV_BACK) return IW_NAV_BUSY;
        nav->pending_back = true;
        return IW_NAV_COALESCED;
    }
    if (actual->busy) return IW_NAV_BUSY;
    if (action == IW_NAV_BACK) target = actual->back;
    const iw_route_descriptor_t *route = iw_route_find(target.page_id);
    if (!route || !actual->current.page_id || iw_route_equal(actual->current, target)) return IW_NAV_INVALID;
    if (route->support != IW_ROUTE_READY && !(action == IW_NAV_BACK && route->support == IW_ROUTE_LEGACY))
        return IW_NAV_UNAVAILABLE;
    /* 返回必须能离开故障页；能力准入只约束新建目标。 */
    if (action == IW_NAV_PUSH && (capabilities & route->required_capabilities) != route->required_capabilities)
        return IW_NAV_UNAVAILABLE;
    if (!actual->depth || !actual->running_apps || actual->running_apps > IW_NAV_MAX_APPS ||
        actual->depth > IW_NAV_MAX_DEPTH || (action == IW_NAV_PUSH &&
        ((actual->new_app && actual->running_apps == IW_NAV_MAX_APPS) ||
         (!actual->new_app && actual->depth == IW_NAV_MAX_DEPTH)))) return IW_NAV_CAPACITY;
    if (nav->sequence == UINT32_MAX) return IW_NAV_EXHAUSTED;
    nav->sequence++;
    nav->state = IW_NAV_PREPARING;
    nav->action = action;
    nav->current = actual->current;
    nav->candidate = target;
    nav->resumed = nav->finished = false;
    memset(&nav->leaving, 0, sizeof(nav->leaving));
    nav->leaving.route = actual->current;
    return IW_NAV_OK;
}

bool iw_nav_save_leaving(iw_navigator_t *nav, const iw_page_resume_t *resume)
{
    if (!nav || !resume || nav->state != IW_NAV_PREPARING ||
        !iw_route_equal(nav->current, resume->route) || resume->draft_bytes > IW_NAV_DRAFT_BYTES) return false;
    nav->leaving = *resume;
    return true;
}

static bool active(const iw_navigator_t *nav, uint32_t sequence)
{
    return nav && nav->sequence == sequence && nav->state == IW_NAV_TRANSITIONING;
}

static void drop_history(iw_navigator_t *nav, unsigned index)
{
    if (nav->previews[index] && nav->preview_ops) nav->preview_ops->release(nav->previews[index]);
    unsigned remaining = nav->history_count - index - 1u;
    memmove(&nav->history[index], &nav->history[index + 1], remaining * sizeof(nav->history[0]));
    memmove(&nav->previews[index], &nav->previews[index + 1], remaining * sizeof(nav->previews[0]));
    nav->previews[--nav->history_count] = 0;
    memset(&nav->history[nav->history_count], 0, sizeof(nav->history[0]));
}

void iw_nav_prepared(iw_navigator_t *nav, uint32_t sequence)
{
    if (nav && nav->sequence == sequence && nav->state == IW_NAV_PREPARING) nav->state = IW_NAV_TRANSITIONING;
}

static void commit(iw_navigator_t *nav)
{
    if (!nav->resumed || !nav->finished) return;
    /* 每个 App 只保留最近页面摘要；历史不作为框架返回栈。 */
    const iw_route_descriptor_t *leaving = iw_route_find(nav->leaving.route.page_id);
    unsigned index = 0;
    while (index < nav->history_count) {
        const iw_route_descriptor_t *previous = iw_route_find(nav->history[index].route.page_id);
        if (leaving && previous && leaving->app_id == previous->app_id) break;
        index++;
    }
    if (index < nav->history_count) drop_history(nav, index);
    if (nav->history_count == IW_NAV_HISTORY) drop_history(nav, 0);
    nav->previews[nav->history_count] = nav->preview_ops ? nav->preview_ops->capture(nav->leaving.route) : 0;
    nav->history[nav->history_count++] = nav->leaving;
    nav->current = nav->candidate;
    nav->state = IW_NAV_COMMITTED;
    increment(&nav->committed);
}

void iw_nav_resumed(iw_navigator_t *nav, uint32_t sequence, iw_route_t route)
{
    if (!active(nav, sequence) || !iw_route_equal(nav->candidate, route)) return;
    nav->resumed = true;
    commit(nav);
}

void iw_nav_finished(iw_navigator_t *nav, uint32_t sequence, bool success)
{
    if (!active(nav, sequence)) return;
    if (!success) { iw_nav_abort(nav, sequence); return; }
    nav->finished = true;
    commit(nav);
}

void iw_nav_abort(iw_navigator_t *nav, uint32_t sequence)
{
    if (!nav || nav->sequence != sequence ||
        (nav->state != IW_NAV_PREPARING && nav->state != IW_NAV_TRANSITIONING)) return;
    nav->state = IW_NAV_ABORTED;
    increment(&nav->aborted);
}

bool iw_nav_settle(iw_navigator_t *nav)
{
    if (!nav || (nav->state != IW_NAV_COMMITTED && nav->state != IW_NAV_ABORTED)) return false;
    nav->state = IW_NAV_IDLE;
    nav->resumed = nav->finished = false;
    return true;
}

bool iw_nav_take_back(iw_navigator_t *nav)
{
    if (!nav || nav->state != IW_NAV_IDLE || !nav->pending_back) return false;
    nav->pending_back = false;
    return true;
}

const iw_page_resume_t *iw_nav_resume_find(const iw_navigator_t *nav, iw_route_t route)
{
    if (nav) for (unsigned i = 0; i < nav->history_count; i++)
        if (iw_route_equal(nav->history[i].route, route)) return &nav->history[i];
    return NULL;
}

bool iw_nav_set_previews(iw_navigator_t *nav, const iw_nav_preview_ops_t *ops)
{
    if (!nav || nav->state != IW_NAV_IDLE || nav->history_count || (ops && (!ops->capture || !ops->release))) return false;
    nav->preview_ops = ops;
    return true;
}

bool iw_nav_history_tile(const iw_navigator_t *nav, unsigned index, iw_nav_history_tile_t *tile)
{
    if (!nav || !tile || index >= nav->history_count) return false;
    const iw_route_descriptor_t *route = iw_route_find(nav->history[index].route.page_id);
    if (!route) return false;
    *tile = (iw_nav_history_tile_t){nav->previews[index], route->page_id, route->name};
    return true;
}

bool iw_nav_clear_history(iw_navigator_t *nav)
{
    if (!nav || nav->state != IW_NAV_IDLE) return false;
    while (nav->history_count) drop_history(nav, nav->history_count - 1u);
    return true;
}
