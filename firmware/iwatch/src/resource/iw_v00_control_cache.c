#include "iw_v00_control_cache.h"

bool iw_v00_control_cache_prepare(iw_v00_control_cache_t *cache,
                                  const iw_v00_control_cache_ops_t *ops)
{
    if (!cache || !ops || !ops->validate_asset || !ops->allocate ||
        !ops->release || !ops->flush || !ops->render_idle) return false;
    if (cache->pixels) return true;
    for (unsigned index = 0; index < 37u; ++index)
        if (!ops->validate_asset(ops->context, index)) return false;

    uint16_t *pixels = ops->allocate(ops->context, IW_V00_CONTROL_BYTES);
    if (!pixels) return false;
    for (uint16_t row = 0; row < IW_V00_CONTROL_HEIGHT; ++row)
        if (!iw_v00_control_background_line(row,
                pixels + (size_t)row * IW_V00_CONTROL_WIDTH, IW_V00_CONTROL_WIDTH)) {
            ops->release(ops->context, pixels);
            return false;
        }
    ops->flush(ops->context, pixels, IW_V00_CONTROL_BYTES);
    cache->pixels = pixels;
    return true;
}

bool iw_v00_control_cache_release(iw_v00_control_cache_t *cache,
                                  const iw_v00_control_cache_ops_t *ops)
{
    if (!cache || !ops || !ops->release || !ops->render_idle) return false;
    if (!cache->pixels) return true;
    if (!ops->render_idle(ops->context)) return false;
    ops->release(ops->context, cache->pixels);
    cache->pixels = NULL;
    return true;
}
