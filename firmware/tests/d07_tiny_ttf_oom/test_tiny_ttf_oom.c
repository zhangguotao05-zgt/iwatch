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

/* 字体服务测试复用同一分配器及实际 LVGL，避免另造一条简化分配路径。 */
void test_font_arm_failure(size_t index) { arm_failure(index); }
size_t test_font_live_bytes(void) { return live_bytes; }
size_t test_font_live_blocks(void) { return live_blocks; }
size_t test_font_allocation_sequence(void) { return allocation_sequence; }
unsigned test_font_assert_count(void) { return assert_count; }
int test_font_service(const void *data, size_t size, const char *stage, size_t number);

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

static uint32_t test_utf8_next(const char * text, uint32_t * offset)
{
    const uint8_t * bytes = (const uint8_t *)text;
    uint32_t index = offset ? *offset : 0;
    uint32_t codepoint = bytes[index++];
    if(codepoint < 0x80) {
        if(offset) *offset = index;
        return codepoint;
    }

    unsigned trailing = codepoint < 0xe0 ? 1u : codepoint < 0xf0 ? 2u : 3u;
    codepoint &= trailing == 1u ? 0x1fu : trailing == 2u ? 0x0fu : 0x07u;
    while(trailing--) codepoint = (codepoint << 6) | (bytes[index++] & 0x3fu);
    if(offset) *offset = index;
    return codepoint;
}

int test_epic_glyph(lv_font_glyph_dsc_t *glyph);
int test_epic_result(void);
unsigned test_epic_submissions(void);

static int run_epic_oom(const uint8_t *data, size_t size, size_t failure_index)
{
    size_t initial_blocks = live_blocks;
    size_t initial_bytes = live_bytes;
    lv_font_t *font = create_font(data, size, 96);
    lv_font_glyph_dsc_t glyph = {0};
    if(!font || !lv_font_get_glyph_dsc(font, &glyph, 0x601d, 0)) return 21;
    size_t before_sequence = allocation_sequence;
    unsigned before_submissions = test_epic_submissions();
    arm_failure(failure_index);
    int pixels = test_epic_glyph(&glyph);
    disarm_failure();
    size_t allocations = allocation_sequence - before_sequence;
    bool expected = pixels == 0;
    if(failure_index) {
        expected = expected && oom_count == 1 &&
                   last_oom_reason == LV_TINY_TTF_OOM_GLYPH_BITMAP &&
                   test_epic_submissions() == before_submissions;
        /* 同一字形再次到达绘制入口时，不应重复上报或提交残缺像素。 */
        test_epic_glyph(&glyph);
        expected = expected && oom_count == 1 &&
                   test_epic_submissions() == before_submissions;
    }
    else expected = expected && test_epic_submissions() == before_submissions + 1;
    lv_tiny_ttf_destroy(font);
    int cleanup = finish_case("epic", allocations, initial_blocks, initial_bytes);
    return expected ? cleanup : 22;
}

static int run_multi_font_evict(const uint8_t * data, size_t size, size_t iterations, bool draw_pixels)
{
    static const int32_t font_sizes[] = {16, 18, 20, 22, 24, 26, 28};
    static const char * texts[] = {
        "Notifications",
        "思澈科技欢迎您",
        "关于我们",
        "思澈科技是一家专注于物联网技术的公司，提供一站式的物联网解决方案。最先提出嵌入式MCU+GPU的物联网解决方案，为客户提供更高性能、更低功耗的物联网产品。",
        "我们是谁",
        "思澈科技成立於2019年3月，總部位於上海張江高科技園區，在重慶、北京、深圳、蘇州均設有分子公司，團隊成員均來自於美國、中國的一線電晶體設計企業，包括Marvell、 Broadcom、Amazon、 紫光展銳、聯發科等，碩士以上學歷占比超過80%； 團隊骨幹具有豐富的產品定義->自主研發->大規模量產的全流程經驗，由這些骨幹成員主導研發的晶片累計出貨超過10億顆。"
    };
    lv_font_t * fonts[sizeof(font_sizes) / sizeof(font_sizes[0])] = {0};
    lv_font_t * references[sizeof(font_sizes) / sizeof(font_sizes[0])] = {0};
    size_t initial_blocks = live_blocks;
    size_t initial_bytes = live_bytes;

    for(size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        fonts[i] = create_font(data, size, font_sizes[i]);
        if(fonts[i] == NULL) return 14;
        if(draw_pixels) {
            references[i] = lv_tiny_ttf_create_data_ex(data, size, font_sizes[i], LV_FONT_KERNING_NORMAL, 0);
            if(references[i] == NULL) return 17;
        }
    }

    for(size_t pass = 0; pass < iterations; pass++) {
        for(size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
            for(size_t text_index = 0; text_index < sizeof(texts) / sizeof(texts[0]); text_index++) {
                uint32_t offset = 0;
                while(texts[text_index][offset] != '\0') {
                    uint32_t codepoint = test_utf8_next(texts[text_index], &offset);
                    uint32_t next = texts[text_index][offset] != '\0' ?
                                    test_utf8_next(&texts[text_index][offset], NULL) : 0;
                    lv_font_glyph_dsc_t glyph;
                    memset(&glyph, 0, sizeof(glyph));
                    if(!lv_font_get_glyph_dsc(fonts[i], &glyph, codepoint, next)) continue;
                    if(draw_pixels && glyph.box_w && glyph.box_h) {
                        /* 对比缓存字形与重新光栅化的字形，随后让真实 EPIC 适配层提交像素。 */
                        lv_font_glyph_dsc_t reference = {0};
                        if(!lv_font_get_glyph_dsc(references[i], &reference, codepoint, next)) return 18;
                        const lv_draw_buf_t * actual = lv_font_get_glyph_bitmap(&glyph, NULL);
                        const lv_draw_buf_t * expected = lv_font_get_glyph_bitmap(&reference, NULL);
                        if(!actual || !expected || glyph.box_w != reference.box_w ||
                           glyph.box_h != reference.box_h || glyph.adv_w != reference.adv_w) return 19;
                        for(uint32_t row = 0; row < glyph.box_h; row++) {
                            if(memcmp(actual->data + row * actual->header.stride,
                                      expected->data + row * expected->header.stride, glyph.box_w)) return 20;
                        }
                        lv_font_glyph_release_draw_data(&reference);
                        lv_font_glyph_release_draw_data(&glyph);
                        (void)test_epic_glyph(&glyph);
                    }
                }
            }
        }
    }

    for(size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        lv_tiny_ttf_destroy(fonts[i]);
        if(references[i]) lv_tiny_ttf_destroy(references[i]);
    }
    int ok = assert_count == 0 && oom_count == 0 &&
             live_blocks == initial_blocks && live_bytes == initial_bytes;
    if(draw_pixels && test_epic_result() != 0) ok = 0;
    printf("stage=%s passes=%zu oom=%u asserts=%u live_blocks=%zu live_bytes=%zu result=%s\n",
           draw_pixels ? "pixels" : "evict", iterations, oom_count, assert_count, live_blocks,
           live_bytes, ok ? "ok" : "failed");
    return ok ? 0 : 16;
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
        fprintf(stderr, "usage: test_tiny_ttf_oom <font.ttf> <create|metadata|bitmap|epic|repeat|evict|pixels|compat|registry|registry_oom|registry_epic> <number>\n");
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
    else if(strcmp(argv[2], "epic") == 0) result = run_epic_oom(font_data, font_size, (size_t)parsed);
    else if(strcmp(argv[2], "repeat") == 0) result = run_repeat(font_data, font_size, (size_t)parsed);
    else if(strcmp(argv[2], "evict") == 0) result = run_multi_font_evict(font_data, font_size, (size_t)parsed, false);
    else if(strcmp(argv[2], "pixels") == 0) result = run_multi_font_evict(font_data, font_size, (size_t)parsed, true);
    else if(strcmp(argv[2], "compat") == 0) result = run_draw_buf_compat();
    else if(strncmp(argv[2], "registry", 8) == 0) result = test_font_service(font_data, font_size, argv[2], (size_t)parsed);
    else result = 67;

    lv_tiny_ttf_set_oom_cb(NULL, NULL);
    lv_deinit();
    free(font_data);
    return result;
}
