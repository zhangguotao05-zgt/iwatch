#ifndef IW_V00_CONTROL_CACHE_H
#define IW_V00_CONTROL_CACHE_H

#include "iw_v00_control_background.h"

enum { IW_V00_CONTROL_BYTES = IW_V00_CONTROL_WIDTH * IW_V00_CONTROL_HEIGHT * 2u };

typedef struct {
    void *context;
    bool (*validate_asset)(void *context, unsigned index);
    void *(*allocate)(void *context, size_t bytes);
    void (*release)(void *context, void *buffer);
    void (*flush)(void *context, void *buffer, size_t bytes);
    bool (*render_idle)(void *context);
} iw_v00_control_cache_ops_t;

typedef struct {
    uint16_t *pixels;
} iw_v00_control_cache_t;

/* 背景缓存只构造一次；绘制任务可能跨帧引用，释放必须等待渲染完全空闲。 */
bool iw_v00_control_cache_prepare(iw_v00_control_cache_t *cache,
                                  const iw_v00_control_cache_ops_t *ops);
bool iw_v00_control_cache_release(iw_v00_control_cache_t *cache,
                                  const iw_v00_control_cache_ops_t *ops);

#endif
