#include "iw_notifications.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_clear(void)
{
    static iw_notification_store_t store, before;
    assert(iw_notification_clear(NULL, 0) == IW_NOTIFICATION_INVALID);
    before = store;
    assert(iw_notification_clear(&store, 0) == IW_NOTIFICATION_OK);
    assert(!memcmp(&before, &store, sizeof(store)));
    uint32_t first_id;
    assert(iw_notification_insert(&store, IW_NOTIFICATION_LOCAL,
        "first", 5, true, 42, &first_id) == IW_NOTIFICATION_OK);
    uint32_t confirmation = store.last_revision;
    assert(iw_notification_insert(&store, IW_NOTIFICATION_LOCAL,
        "second", 6, true, 43, NULL) == IW_NOTIFICATION_OK);
    before = store;
    assert(iw_notification_clear(&store, confirmation) == IW_NOTIFICATION_STALE);
    assert(!memcmp(&before, &store, sizeof(store)));
    confirmation = store.last_revision;
    assert(iw_notification_mark_read(&store, first_id, store.records[0].revision) == IW_NOTIFICATION_OK);
    before = store;
    assert(iw_notification_clear(&store, confirmation) == IW_NOTIFICATION_STALE);
    assert(!memcmp(&before, &store, sizeof(store)));
    confirmation = store.last_revision;
    assert(iw_notification_delete(&store, first_id, store.records[0].revision) == IW_NOTIFICATION_OK);
    before = store;
    assert(iw_notification_clear(&store, confirmation) == IW_NOTIFICATION_STALE);
    assert(!memcmp(&before, &store, sizeof(store)));
    /* 成功只清记录；身份和淘汰计数不重置，旧 ID 永不重新指向新通知。 */
    assert(iw_notification_clear(&store, store.last_revision) == IW_NOTIFICATION_OK);
    assert(!store.count && store.last_revision == before.last_revision + 1);
    assert(store.last_id == before.last_id && store.last_sequence == before.last_sequence &&
           store.evicted == before.evicted);
    static const iw_notification_t empty[IW_NOTIFICATION_CAPACITY] = {0};
    assert(!memcmp(store.records, empty, sizeof(empty)));
    before = store;
    assert(iw_notification_clear(&store, store.last_revision) == IW_NOTIFICATION_OK);
    assert(!memcmp(&before, &store, sizeof(store)));
    uint32_t new_id;
    assert(iw_notification_insert(&store, IW_NOTIFICATION_LOCAL,
        "new", 3, false, 0, &new_id) == IW_NOTIFICATION_OK);
    assert(new_id > before.last_id && !iw_notification_find(&store, first_id));
    store.last_revision = UINT32_MAX;
    before = store;
    assert(iw_notification_clear(&store, UINT32_MAX) == IW_NOTIFICATION_EXHAUSTED);
    assert(!memcmp(&before, &store, sizeof(store)));
    store.count = IW_NOTIFICATION_CAPACITY + 1;
    before = store;
    assert(iw_notification_clear(&store, UINT32_MAX) == IW_NOTIFICATION_INVALID);
    assert(!memcmp(&before, &store, sizeof(store)));
    puts("V01-01 clear: insert/read/delete conflicts, empty, monotonic IDs, exhaustion atomicity passed");
}

int main(void)
{
    test_clear();
    static iw_notification_store_t store;
    uint32_t id = 0;
    const char utf8[] = "中文测试";
    assert(iw_notification_insert(&store, IW_NOTIFICATION_DIAGNOSTIC,
        utf8, strlen(utf8), false, 123, &id) == IW_NOTIFICATION_OK);
    assert(id == 1 && store.count == 1 && !store.records[0].time_valid &&
        store.records[0].received_utc == 0);
    assert(!strcmp(store.records[0].text, utf8));
    uint32_t revision = store.records[0].revision;
    assert(iw_notification_mark_read(&store, id, revision + 1) == IW_NOTIFICATION_STALE);
    assert(!store.records[0].read);
    assert(iw_notification_mark_read(&store, id, revision) == IW_NOTIFICATION_OK);
    assert(store.records[0].revision != revision);
    assert(iw_notification_delete(&store, id, revision) == IW_NOTIFICATION_STALE);
    assert(iw_notification_delete(&store, id, store.records[0].revision) == IW_NOTIFICATION_OK);
    assert(!store.count && !iw_notification_find(&store, id));

    char long_text[260];
    memset(long_text, 'a', 253);
    memcpy(long_text + 253, "中", 3);
    long_text[256] = 'b';
    assert(iw_notification_insert(&store, IW_NOTIFICATION_LOCAL,
        long_text, 257, true, 42, &id) == IW_NOTIFICATION_OK);
    assert(strlen(store.records[0].text) == 253);
    assert(store.records[0].received_utc == 42);
    iw_notification_store_t before = store;
    const unsigned char invalid[] = {0xe0u, 0x80u, 0x80u};
    assert(iw_notification_insert(&store, IW_NOTIFICATION_LOCAL,
        (const char *)invalid, sizeof(invalid), true, 0, NULL) == IW_NOTIFICATION_INVALID);
    assert(!memcmp(&before, &store, sizeof(store)));

    memset(&store, 0, sizeof(store));
    for (unsigned i = 0; i < IW_NOTIFICATION_CAPACITY; i++)
        assert(iw_notification_insert(&store, IW_NOTIFICATION_LOCAL,
            "a", 1, true, i, NULL) == IW_NOTIFICATION_OK);
    assert(iw_notification_mark_read(&store, 8, store.records[7].revision) == IW_NOTIFICATION_OK);
    assert(iw_notification_insert(&store, IW_NOTIFICATION_LOCAL,
        "b", 1, true, 100, &id) == IW_NOTIFICATION_OK);
    assert(store.count == IW_NOTIFICATION_CAPACITY && store.evicted == 1 &&
        !iw_notification_find(&store, 8) && iw_notification_find(&store, 1) && id == 33);
    assert(iw_notification_insert(&store, IW_NOTIFICATION_LOCAL,
        "c", 1, true, 101, &id) == IW_NOTIFICATION_OK);
    assert(!iw_notification_find(&store, 1) && store.evicted == 2 && id == 34);
    before = store;
    store.last_revision = UINT32_MAX;
    before = store;
    assert(iw_notification_insert(&store, IW_NOTIFICATION_LOCAL,
        "d", 1, true, 0, NULL) == IW_NOTIFICATION_EXHAUSTED);
    assert(!memcmp(&before, &store, sizeof(store)));
    puts("D13-B 普通通知：UTF-8 截断、陈旧版本、容量淘汰和饱和无副作用通过");
    return 0;
}
