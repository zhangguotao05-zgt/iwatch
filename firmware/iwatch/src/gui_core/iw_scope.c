#include "iw_scope.h"
#include <string.h>

static const uint8_t limits[IW_SCOPE_KIND_COUNT] = {
    IW_SCOPE_CALLBACKS, IW_SCOPE_TIMERS, IW_SCOPE_ANIMATIONS, IW_SCOPE_HANDLES
};

bool iw_scope_init(iw_scope_t *scope, uint16_t page_id, uint32_t generation)
{
    if (!scope || scope->alive || scope->cleaning || !page_id || !generation) return false;
    memset(scope, 0, sizeof(*scope));
    scope->token = (iw_page_token_t){page_id, generation};
    scope->alive = true;
    return true;
}

bool iw_scope_accepts(const iw_scope_t *scope, iw_page_token_t token)
{
    return scope && scope->alive && scope->token.page_id == token.page_id &&
           scope->token.generation == token.generation;
}

bool iw_scope_add(iw_scope_t *scope, iw_scope_kind_t kind, void *resource,
                  iw_scope_action_t release, iw_scope_action_t pause, iw_scope_action_t resume)
{
    if (!scope || !scope->alive || scope->cleaning || (unsigned)kind >= IW_SCOPE_KIND_COUNT ||
        !resource || !release || scope->counts[kind] == limits[kind]) return false;
    if ((kind == IW_SCOPE_TIMER || kind == IW_SCOPE_ANIMATION) && (!pause || !resume)) return false;
    for (unsigned i = 0; i < scope->count; i++) if (scope->entries[i].resource == resource) return false;
    scope->entries[scope->count++] = (iw_scope_entry_t){resource, release, pause, resume, (uint8_t)kind};
    scope->counts[kind]++;
    if (!scope->visible && pause) {
        scope->cleaning = true;
        pause(resource);
        scope->cleaning = false;
    }
    return true;
}

bool iw_scope_remove(iw_scope_t *scope, void *resource)
{
    if (!scope || !scope->alive || scope->cleaning || !resource) return false;
    for (unsigned i = 0; i < scope->count; i++) {
        if (scope->entries[i].resource != resource) continue;
        scope->counts[scope->entries[i].kind]--;
        scope->count--;
        memmove(&scope->entries[i], &scope->entries[i + 1],
                (scope->count - i) * sizeof(scope->entries[0]));
        memset(&scope->entries[scope->count], 0, sizeof(scope->entries[0]));
        return true;
    }
    return false;
}

void iw_scope_pause(iw_scope_t *scope)
{
    if (!scope || !scope->alive || !scope->visible || scope->cleaning) return;
    scope->visible = false;
    scope->cleaning = true;
    for (unsigned i = scope->count; i; i--) {
        iw_scope_entry_t *entry = &scope->entries[i - 1];
        if (entry->pause) entry->pause(entry->resource);
    }
    scope->cleaning = false;
}

void iw_scope_resume(iw_scope_t *scope)
{
    if (!scope || !scope->alive || scope->visible || scope->cleaning) return;
    scope->visible = true;
    scope->cleaning = true;
    for (unsigned i = 0; i < scope->count; i++) {
        iw_scope_entry_t *entry = &scope->entries[i];
        if (entry->resume) entry->resume(entry->resource);
    }
    scope->cleaning = false;
}

void iw_scope_stop(iw_scope_t *scope)
{
    if (!scope || !scope->alive || scope->cleaning) return;
    scope->alive = false;
    scope->visible = false;
    scope->cleaning = true;
    for (unsigned kind = 0; kind < IW_SCOPE_KIND_COUNT; kind++) {
        for (unsigned i = scope->count; i; i--) {
            iw_scope_entry_t *entry = &scope->entries[i - 1];
            if (entry->kind == kind) entry->release(entry->resource);
        }
    }
    scope->count = 0;
    memset(scope->counts, 0, sizeof(scope->counts));
    memset(scope->entries, 0, sizeof(scope->entries));
    scope->cleaning = false;
}
