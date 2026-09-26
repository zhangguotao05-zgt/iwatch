#include "iw_v00_paint_cache.h"
#include "iw_font_port.h"
#include <limits.h>
#ifdef IW_TARGET_BUILD
#include "app_mem.h"
#include <rtthread.h>
extern struct rt_memheap app_image_psram_memheap;
#endif

_Static_assert(IW_V00_PAINT_COUNT - 1u <= 8u, "Paint ownership mask must fit uint8_t");

typedef struct {
    lv_image_dsc_t image;
    uint16_t references;
} paint_slot_t;
static paint_slot_t slots[IW_V00_PAINT_COUNT - 1u];

static void release_pixels(void *pixels)
{
#ifdef IW_TARGET_BUILD
    app_cache_free(pixels);
#else
    lv_free(pixels);
#endif
}

static void *allocate_pixels(size_t bytes)
{
#ifdef IW_TARGET_BUILD
    void *pixels = app_cache_alloc(bytes, IMAGE_CACHE_PSRAM);
    if (!pixels) return NULL;
    /* SDK 会回退系统堆；发布前按实际专用堆边界拒绝该回退，不读取私有分配头。 */
    uintptr_t start = (uintptr_t)app_image_psram_memheap.start_addr;
    uintptr_t address = (uintptr_t)pixels;
    size_t pool = app_image_psram_memheap.pool_size;
    if (!start || address < start || address - start > pool || bytes > pool - (address - start)) {
        app_cache_free(pixels);
        return NULL;
    }
    return pixels;
#else
    return lv_malloc(bytes);
#endif
}

bool iw_v00_paint_retain(uint8_t *references, unsigned id)
{
    const iw_v00_paint_spec_t *s = iw_v00_paint_spec(id);
    if (!references || (*references & ~IW_V00_PAINT_MASK) || !s || !iw_font_port_is_owner() || !iw_font_port_render_idle()) return false;
    uint8_t bit = (uint8_t)(1u << (id - 1u));
    if (*references & bit) return true;
    paint_slot_t *slot = &slots[id - 1u];
    if (slot->references == UINT16_MAX) return false;
    if (!slot->image.data) {
        size_t bytes = (size_t)s->width * s->height * 3u;
        size_t allocated = (bytes + 3u) & ~(size_t)3u;
        uint8_t *pixels = allocate_pixels(allocated);
        if (!pixels) return false;
        if (!iw_v00_paint_pixels(id, pixels, bytes)) {
            release_pixels(pixels);
            return false;
        }
        /* 尾部对齐字节初始化后随有效数据一起 clean；描述符只声明真实像素长度。 */
        for (size_t i = bytes; i < allocated; ++i) pixels[i] = 0;
#ifdef IW_TARGET_BUILD
        app_mem_flush_cache(pixels, (uint32_t)allocated);
#endif
        slot->image = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565A8, .w = s->width, .h = s->height,
            .stride = s->width * 2u}, .data_size = (uint32_t)bytes, .data = pixels};
    }
    ++slot->references;
    *references |= bit;
    return true;
}

bool iw_v00_paint_release(uint8_t *references)
{
    if (!references || (*references & ~IW_V00_PAINT_MASK) || !iw_font_port_is_owner()) return false;
    if (!*references) return true;
    if (!iw_font_port_render_idle()) return false;
    for (unsigned i = 0; i < IW_V00_PAINT_COUNT - 1u; ++i) {
        if (!(*references & (1u << i))) continue;
        paint_slot_t *slot = &slots[i];
        if (!slot->references) return false;
        if (--slot->references == 0u) {
            release_pixels((void *)slot->image.data);
            slot->image = (lv_image_dsc_t){0};
        }
        *references &= (uint8_t)~(1u << i);
    }
    return true;
}

const lv_image_dsc_t *iw_v00_paint_image(uint8_t references, unsigned id)
{
    if (!iw_font_port_is_owner() || !iw_v00_paint_spec(id) || !(references & (1u << (id - 1u)))) return NULL;
    const paint_slot_t *slot = &slots[id - 1u];
    return slot->references && slot->image.data ? &slot->image : NULL;
}
