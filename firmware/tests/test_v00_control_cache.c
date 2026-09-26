#include "iw_v00_control_cache.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned validated, allocated, released, flushed;
    int fail_asset;
    bool fail_allocation, idle;
} fixture_t;

static bool validate_asset(void *context, unsigned index)
{
    fixture_t *fixture = context;
    ++fixture->validated;
    return (int)index != fixture->fail_asset;
}

static void *allocate(void *context, size_t bytes)
{
    fixture_t *fixture = context;
    ++fixture->allocated;
    return fixture->fail_allocation ? NULL : malloc(bytes);
}

static void release(void *context, void *buffer)
{
    fixture_t *fixture = context;
    ++fixture->released;
    free(buffer);
}

static void flush(void *context, void *buffer, size_t bytes)
{
    fixture_t *fixture = context;
    assert(buffer && bytes == IW_V00_CONTROL_BYTES);
    ++fixture->flushed;
}

static bool render_idle(void *context)
{
    return ((fixture_t *)context)->idle;
}

int main(void)
{
    fixture_t fixture = {.fail_asset = 5, .idle = true};
    iw_v00_control_cache_t cache = {0};
    iw_v00_control_cache_ops_t ops = {
        &fixture, validate_asset, allocate, release, flush, render_idle
    };
    /* 损坏资源或解码预检失败不得分配背景。 */
    assert(!iw_v00_control_cache_prepare(&cache, &ops));
    assert(fixture.validated == 6u && fixture.allocated == 0u && !cache.pixels);

    fixture.fail_asset = -1;
    fixture.fail_allocation = true;
    assert(!iw_v00_control_cache_prepare(&cache, &ops));
    assert(fixture.allocated == 1u && fixture.flushed == 0u && !cache.pixels);

    fixture.fail_allocation = false;
    assert(iw_v00_control_cache_prepare(&cache, &ops));
    uint16_t *original = cache.pixels;
    assert(original && fixture.allocated == 2u && fixture.flushed == 1u);
    for (unsigned i = 0; i < 1000u; ++i) {
        assert(iw_v00_control_cache_prepare(&cache, &ops));
        assert(cache.pixels == original);
    }
    assert(fixture.allocated == 2u && fixture.flushed == 1u);

    /* GPU/LCD 忙或超时都不允许释放仍被任务引用的像素。 */
    fixture.idle = false;
    for (unsigned i = 0; i < 1000u; ++i)
        assert(!iw_v00_control_cache_release(&cache, &ops));
    assert(cache.pixels == original && fixture.released == 0u);
    fixture.idle = true;
    assert(iw_v00_control_cache_release(&cache, &ops));
    assert(!cache.pixels && fixture.released == 1u);
    assert(iw_v00_control_cache_release(&cache, &ops));
    assert(fixture.released == 1u);

    /* 退出后重新进入，缓存按需重建且不叠加长期占用。 */
    assert(iw_v00_control_cache_prepare(&cache, &ops));
    assert(fixture.allocated == 3u);
    assert(iw_v00_control_cache_release(&cache, &ops));
    assert(fixture.released == 2u);
    return 0;
}
