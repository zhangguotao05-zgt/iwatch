#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_MEMORY_MAGIC 0x49575446u

typedef struct
{
    uint32_t magic;
    size_t size;
} test_memory_header_t;

static size_t allocation_sequence;
static size_t failure_sequence = SIZE_MAX;
static size_t live_blocks;
static size_t live_bytes;
static unsigned assert_count;
static unsigned oom_count;
static lv_tiny_ttf_oom_reason_t last_oom_reason;

static void arm_failure(size_t relative_index)
{
    failure_sequence = relative_index ? allocation_sequence + relative_index : SIZE_MAX;
}

static void disarm_failure(void)
{
    failure_sequence = SIZE_MAX;
}

void tiny_ttf_test_assert_handler(void)
{
    assert_count++;
}

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void * memory, size_t bytes)
{
    LV_UNUSED(memory);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void * lv_malloc_core(size_t size)
{
    allocation_sequence++;
    if(allocation_sequence == failure_sequence) return NULL;

    test_memory_header_t * header = malloc(sizeof(*header) + size);
    if(header == NULL) return NULL;
    header->magic = TEST_MEMORY_MAGIC;
    header->size = size;
    live_blocks++;
    live_bytes += size;
    return header + 1;
}

void * lv_realloc_core(void * memory, size_t new_size)
{
    if(memory == NULL) return lv_malloc_core(new_size);

    test_memory_header_t * old_header = (test_memory_header_t *)memory - 1;
    if(old_header->magic != TEST_MEMORY_MAGIC) abort();

    allocation_sequence++;
    if(allocation_sequence == failure_sequence) return NULL;

    size_t old_size = old_header->size;
    test_memory_header_t * new_header = realloc(old_header, sizeof(*new_header) + new_size);
    if(new_header == NULL) return NULL;
    new_header->magic = TEST_MEMORY_MAGIC;
    new_header->size = new_size;
    live_bytes = live_bytes - old_size + new_size;
    return new_header + 1;
}

void lv_free_core(void * memory)
{
    if(memory == NULL) return;
    test_memory_header_t * header = (test_memory_header_t *)memory - 1;
    if(header->magic != TEST_MEMORY_MAGIC) abort();
    header->magic = 0;
    live_blocks--;
    live_bytes -= header->size;
    free(header);
}

void lv_mem_monitor_core(lv_mem_monitor_t * monitor)
{
    memset(monitor, 0, sizeof(*monitor));
    monitor->used_cnt = live_blocks;
    monitor->total_size = live_bytes;
    monitor->max_used = live_bytes;
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

static void oom_callback(lv_tiny_ttf_oom_reason_t reason, void * user_data)
{
    LV_UNUSED(user_data);
    if(oom_count != UINT32_MAX) oom_count++;
    last_oom_reason = reason;
}

static uint8_t * load_file(const char * path, size_t * size)
{
    FILE * file = fopen(path, "rb");
    if(file == NULL) return NULL;
    if(fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if(length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t * data = malloc((size_t)length);
    if(data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static lv_font_t * create_font(const uint8_t * data, size_t size, int32_t font_size)
{
    return lv_tiny_ttf_create_data_ex(data, size, font_size, LV_FONT_KERNING_NORMAL, 16);
}

static int finish_case(const char * stage, size_t allocations, size_t before_blocks, size_t before_bytes)
{
    int ok = assert_count == 0 && live_blocks == before_blocks && live_bytes == before_bytes;
    printf("stage=%s allocations=%zu oom=%u reason=%d asserts=%u live_blocks=%zu live_bytes=%zu result=%s\n",
           stage, allocations, oom_count, (int)last_oom_reason, assert_count, live_blocks, live_bytes,
           ok ? "ok" : "failed");
    return ok ? 0 : 1;
}

static int run_create(const uint8_t * data, size_t size, size_t failure_index)
{
    size_t before_blocks = live_blocks;
    size_t before_bytes = live_bytes;
    size_t before_sequence = allocation_sequence;
    arm_failure(failure_index);
    lv_font_t * font = create_font(data, size, 28);
    disarm_failure();
    size_t allocations = allocation_sequence - before_sequence;
    if(font) lv_tiny_ttf_destroy(font);
    if(failure_index && oom_count == 0) return 2;
    return finish_case("create", allocations, before_blocks, before_bytes);
}

static int run_metadata(const uint8_t * data, size_t size, size_t failure_index)
{
    size_t initial_blocks = live_blocks;
    size_t initial_bytes = live_bytes;
    lv_font_t * font = create_font(data, size, 28);
    if(font == NULL) return 3;
    size_t before_sequence = allocation_sequence;
    lv_font_glyph_dsc_t glyph;
    memset(&glyph, 0, sizeof(glyph));
    arm_failure(failure_index);
    bool loaded = lv_font_get_glyph_dsc(font, &glyph, 0x601d, 0);
    disarm_failure();
    size_t allocations = allocation_sequence - before_sequence;
    bool blocked_after_oom = true;
    if(failure_index) {
        lv_font_glyph_dsc_t retry;
        memset(&retry, 0, sizeof(retry));
        blocked_after_oom = !lv_font_get_glyph_dsc(font, &retry, 0x601d, 0);
    }
    bool expected = (!failure_index && loaded) ||
                    (failure_index && oom_count != 0 && blocked_after_oom &&
                     last_oom_reason == LV_TINY_TTF_OOM_GLYPH_METADATA);
    lv_tiny_ttf_destroy(font);
    int cleanup_result = finish_case("metadata", allocations, initial_blocks, initial_bytes);
    return expected ? cleanup_result : 4;
}

static int run_bitmap(const uint8_t * data, size_t size, size_t failure_index)
{
    size_t initial_blocks = live_blocks;
    size_t initial_bytes = live_bytes;
    /* 96 px 中文字形会走 STB 动态扫描线，可覆盖大字号专属失败点。 */
    lv_font_t * font = create_font(data, size, 96);
    if(font == NULL) return 5;
    lv_font_glyph_dsc_t glyph;
    memset(&glyph, 0, sizeof(glyph));
    if(!lv_font_get_glyph_dsc(font, &glyph, 0x601d, 0)) {
        lv_tiny_ttf_destroy(font);
        return 6;
    }
    size_t before_sequence = allocation_sequence;
    arm_failure(failure_index);
    const void * bitmap = lv_font_get_glyph_bitmap(&glyph, NULL);
    disarm_failure();
    size_t allocations = allocation_sequence - before_sequence;
    if(bitmap) lv_font_glyph_release_draw_data(&glyph);
    bool expected = !failure_index || (!bitmap && oom_count != 0 &&
                                       last_oom_reason == LV_TINY_TTF_OOM_GLYPH_BITMAP);
    lv_tiny_ttf_destroy(font);
    int cleanup_result = finish_case("bitmap", allocations, initial_blocks, initial_bytes);
    return expected ? cleanup_result : 7;
}

static int run_repeat(const uint8_t * data, size_t size, size_t iterations)
{
    size_t initial_blocks = live_blocks;
    size_t initial_bytes = live_bytes;
    for(size_t index = 0; index < iterations; index++) {
        int32_t font_size = (index & 1u) ? 96 : 28;
        lv_font_t * font = create_font(data, size, font_size);
        if(font == NULL) return 8;
        lv_font_glyph_dsc_t glyph;
        memset(&glyph, 0, sizeof(glyph));
        if(!lv_font_get_glyph_dsc(font, &glyph, 0x601d, 0)) {
            lv_tiny_ttf_destroy(font);
            return 9;
        }
        const void * bitmap = lv_font_get_glyph_bitmap(&glyph, NULL);
        if(bitmap == NULL) {
            lv_tiny_ttf_destroy(font);
            return 10;
        }
        lv_font_glyph_release_draw_data(&glyph);
        lv_tiny_ttf_destroy(font);
        if(live_blocks != initial_blocks || live_bytes != initial_bytes) return 11;
    }
    int ok = assert_count == 0 && oom_count == 0 &&
             live_blocks == initial_blocks && live_bytes == initial_bytes;
    printf("stage=repeat cycles=%zu oom=%u asserts=%u live_blocks=%zu live_bytes=%zu result=%s\n",
           iterations, oom_count, assert_count, live_blocks, live_bytes, ok ? "ok" : "failed");
    return ok ? 0 : 12;
}

static int run_draw_buf_compat(void)
{
    size_t initial_blocks = live_blocks;
    size_t initial_bytes = live_bytes;

    arm_failure(1);
    lv_draw_buf_t * descriptor_failure = lv_draw_buf_create(16, 16, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
    disarm_failure();
    unsigned descriptor_asserts = assert_count;
    if(descriptor_failure) lv_draw_buf_destroy(descriptor_failure);

    assert_count = 0;
    arm_failure(2);
    lv_draw_buf_t * buffer_failure = lv_draw_buf_create(16, 16, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
    disarm_failure();
    unsigned buffer_asserts = assert_count;
    if(buffer_failure) lv_draw_buf_destroy(buffer_failure);

    bool ok = descriptor_failure == NULL && buffer_failure == NULL &&
              descriptor_asserts == 1 && buffer_asserts == 0 &&
              live_blocks == initial_blocks && live_bytes == initial_bytes;
    printf("stage=compat descriptor_asserts=%u buffer_asserts=%u asserts=0 result=%s\n",
           descriptor_asserts, buffer_asserts, ok ? "ok" : "failed");
    return ok ? 0 : 13;
}

int main(int argc, char ** argv)
{
    if(argc != 4) {
        fprintf(stderr, "usage: test_tiny_ttf_oom <font.ttf> <create|metadata|bitmap|repeat|compat> <number>\n");
        return 64;
    }

    char * end = NULL;
    unsigned long parsed = strtoul(argv[3], &end, 10);
    if(end == NULL || *end != '\0') return 65;

    size_t font_size = 0;
    uint8_t * font_data = load_file(argv[1], &font_size);
    if(font_data == NULL) return 66;

    lv_init();
    lv_tiny_ttf_set_oom_cb(oom_callback, NULL);
    oom_count = 0;
    assert_count = 0;
    last_oom_reason = LV_TINY_TTF_OOM_FONT_DESC;

    int result;
    if(strcmp(argv[2], "create") == 0) result = run_create(font_data, font_size, (size_t)parsed);
    else if(strcmp(argv[2], "metadata") == 0) result = run_metadata(font_data, font_size, (size_t)parsed);
    else if(strcmp(argv[2], "bitmap") == 0) result = run_bitmap(font_data, font_size, (size_t)parsed);
    else if(strcmp(argv[2], "repeat") == 0) result = run_repeat(font_data, font_size, (size_t)parsed);
    else if(strcmp(argv[2], "compat") == 0) result = run_draw_buf_compat();
    else result = 67;

    lv_tiny_ttf_set_oom_cb(NULL, NULL);
    lv_deinit();
    free(font_data);
    return result;
}
