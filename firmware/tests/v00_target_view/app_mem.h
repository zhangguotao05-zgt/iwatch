#ifndef TEST_APP_MEM_H
#define TEST_APP_MEM_H
#include <stddef.h>
#include <stdint.h>
enum { IMAGE_CACHE_PSRAM = 1 };
void *app_cache_alloc(size_t bytes, unsigned heap);
void app_cache_free(void *pixels);
void app_mem_flush_cache(void *pixels, uint32_t bytes);
#endif
