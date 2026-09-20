#include "iw_notifications.h"
#include <string.h>

_Static_assert(sizeof(iw_notification_store_t) <= 12u * 1024u,
               "普通通知固定账本不得超过 12 KiB");

static size_t utf8_width(const unsigned char *p, size_t left)
{
    unsigned c = p[0];
    if (c < 0x80u) return c ? 1u : 0u;
    size_t width = c >= 0xc2u && c <= 0xdfu ? 2u :
                   c >= 0xe0u && c <= 0xefu ? 3u :
                   c >= 0xf0u && c <= 0xf4u ? 4u : 0u;
    if (!width || left < width) return 0;
    for (size_t i = 1; i < width; i++) if ((p[i] & 0xc0u) != 0x80u) return 0;
    if (width == 3u && ((c == 0xe0u && p[1] < 0xa0u) ||
                        (c == 0xedu && p[1] >= 0xa0u))) return 0;
    if (width == 4u && ((c == 0xf0u && p[1] < 0x90u) ||
                        (c == 0xf4u && p[1] >= 0x90u))) return 0;
    return width;
}

static bool valid_prefix(const char *text, size_t bytes, size_t *kept)
{
    *kept = 0;
    for (size_t i = 0; i < bytes;) {
        size_t width = utf8_width((const unsigned char *)text + i, bytes - i);
        if (!width) return false;
        if (i + width < IW_NOTIFICATION_TEXT_BYTES) *kept = i + width;
        i += width;
    }
    return *kept != 0;
}

const iw_notification_t *iw_notification_find(const iw_notification_store_t *store, uint32_t id)
{
    if (!store || !id || store->count > IW_NOTIFICATION_CAPACITY) return NULL;
    for (unsigned i = 0; i < store->count; i++)
        if (store->records[i].id == id) return &store->records[i];
    return NULL;
}

iw_notification_result_t iw_notification_insert(iw_notification_store_t *store,
    iw_notification_source_t source, const char *text, size_t bytes,
    bool time_valid, uint32_t received_utc, uint32_t *id)
{
    if (!store || !text || !bytes || bytes > 1024u ||
        store->count > IW_NOTIFICATION_CAPACITY ||
        (source != IW_NOTIFICATION_LOCAL && source != IW_NOTIFICATION_DIAGNOSTIC))
        return IW_NOTIFICATION_INVALID;
    size_t kept;
    if (!valid_prefix(text, bytes, &kept)) return IW_NOTIFICATION_INVALID;
    if (store->last_id == UINT32_MAX || store->last_sequence == UINT32_MAX ||
        store->last_revision == UINT32_MAX) return IW_NOTIFICATION_EXHAUSTED;
    if (store->count == IW_NOTIFICATION_CAPACITY) {
        unsigned oldest = 0;
        while (oldest < store->count && !store->records[oldest].read) oldest++;
        if (oldest == store->count) oldest = 0;
        memmove(&store->records[oldest], &store->records[oldest + 1],
                (store->count - oldest - 1u) * sizeof(store->records[0]));
        store->count--;
        if (store->evicted != UINT32_MAX) store->evicted++;
    }
    iw_notification_t *entry = &store->records[store->count++];
    *entry = (iw_notification_t){.id = ++store->last_id,
        .sequence = ++store->last_sequence, .revision = ++store->last_revision,
        .received_utc = time_valid ? received_utc : 0,
        .time_valid = time_valid, .source = (uint8_t)source};
    memcpy(entry->text, text, kept);
    entry->text[kept] = '\0';
    if (id) *id = entry->id;
    return IW_NOTIFICATION_OK;
}

static iw_notification_t *mutable_find(iw_notification_store_t *store, uint32_t id)
{
    return (iw_notification_t *)iw_notification_find(store, id);
}

iw_notification_result_t iw_notification_mark_read(iw_notification_store_t *store,
    uint32_t id, uint32_t expected_revision)
{
    iw_notification_t *entry = mutable_find(store, id);
    if (!entry || entry->revision != expected_revision) return IW_NOTIFICATION_STALE;
    if (entry->read) return IW_NOTIFICATION_OK;
    if (store->last_revision == UINT32_MAX) return IW_NOTIFICATION_EXHAUSTED;
    entry->read = 1;
    entry->revision = ++store->last_revision;
    return IW_NOTIFICATION_OK;
}

iw_notification_result_t iw_notification_delete(iw_notification_store_t *store,
    uint32_t id, uint32_t expected_revision)
{
    iw_notification_t *entry = mutable_find(store, id);
    if (!entry || entry->revision != expected_revision) return IW_NOTIFICATION_STALE;
    if (store->last_revision == UINT32_MAX) return IW_NOTIFICATION_EXHAUSTED;
    unsigned index = (unsigned)(entry - store->records);
    memmove(entry, entry + 1, (store->count - index - 1u) * sizeof(*entry));
    store->count--;
    memset(&store->records[store->count], 0, sizeof(*entry));
    store->last_revision++;
    return IW_NOTIFICATION_OK;
}
