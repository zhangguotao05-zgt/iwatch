#include "lvgl.h"
#include "src/draw/lv_draw_label_private.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 硬件边界使用独立的 CPU/GPU 视图，只有正确的缓存清理范围才能同步像素。 */
#define LV_USE_DRAW_EPIC 1
#define LV_EPIC_LOG(...) ((void)0)
#define LOG_D(...) ((void)0)
#define PRINT_LAYER_INFO(...) ((void)0)
#define PRINT_LAYER_EXTRA_INFO(...) ((void)0)
#define RT_ASSERT(value) assert(value)
#define RT_NULL NULL
#define RT_EOK 0
#define RT_ERROR 1
#define HAL_OK 0
#define GPU_BLEND_EXP_MS 100
#define DRV_EPIC_LETTER_BLEND 1
#define ALPHA_BLEND_RGBCOLOR 1
#define EPIC_INPUT_A8 8

typedef int rt_err_t;
typedef int HAL_StatusTypeDef;
typedef struct { int unused; } EPIC_HandleTypeDef;
typedef struct { int32_t x0, y0, x1, y1; } EPIC_AreaTypeDef;
typedef struct {
    uint8_t *data;
    uint32_t data_size;
    uint32_t width, height, total_width;
    int32_t x_offset, y_offset;
    uint8_t alpha, color_r, color_g, color_b;
    bool color_en;
    int ax_mode, color_mode;
} EPIC_LayerConfigTypeDef;

static struct {
    EPIC_HandleTypeDef epic_handle;
    bool cont_mode;
} drv_epic;
static uint8_t gpu_pixels[128 * 128];
static const uint8_t *clean_address;
static uint32_t clean_size;
static unsigned pixel_errors, submissions;

static int wait_gpu_done(int timeout) { (void)timeout; return RT_EOK; }

static int mpu_dcache_clean(void *data, uint32_t size)
{
    clean_address = data;
    clean_size = size;
    return 0;
}

static void gpu_lock(int operation, EPIC_LayerConfigTypeDef *fg,
                     EPIC_LayerConfigTypeDef *mask, EPIC_LayerConfigTypeDef *output)
{
    (void)operation;
    (void)mask;
    (void)output;
    /* 首次提交的生产驱动按行跨度和高度计算长度，后续走被测函数的 data_size。 */
    mpu_dcache_clean(fg->data, fg->total_width * fg->height);
}

static int gpu_consume(EPIC_LayerConfigTypeDef *fg)
{
    uint32_t bytes = fg->total_width * fg->height;
    assert(bytes <= sizeof(gpu_pixels));
    memset(gpu_pixels, 0xa5, sizeof(gpu_pixels));
    if(clean_address == fg->data) {
        uint32_t copied = clean_size < bytes ? clean_size : bytes;
        memcpy(gpu_pixels, fg->data, copied);
    }
    for(uint32_t y = 0; y < fg->height; y++) {
        if(memcmp(gpu_pixels + y * fg->total_width, fg->data + y * fg->total_width,
                  fg->width) != 0) {
            pixel_errors++;
            break;
        }
    }
    clean_address = NULL;
    clean_size = 0;
    submissions++;
    return HAL_OK;
}

static int HAL_EPIC_ContBlendStart(EPIC_HandleTypeDef *handle, EPIC_LayerConfigTypeDef *fg,
                                 EPIC_LayerConfigTypeDef *mask, EPIC_LayerConfigTypeDef *output)
{
    (void)handle; (void)mask; (void)output;
    return gpu_consume(fg);
}

static int HAL_EPIC_ContBlendRepeat(EPIC_HandleTypeDef *handle, EPIC_LayerConfigTypeDef *fg,
                                  EPIC_LayerConfigTypeDef *mask, EPIC_LayerConfigTypeDef *output)
{
    (void)handle; (void)mask; (void)output;
    return gpu_consume(fg);
}

#include "epic_driver_under_test.inc"

static void HAL_EPIC_LayerConfigInit(EPIC_LayerConfigTypeDef *layer)
{
    memset(layer, 0, sizeof(*layer));
}

static void drv_epic_cont_blend_reset(void) { drv_epic.cont_mode = false; }
static void lv_epic_print_area_info(const char *name, const lv_area_t *area)
{ (void)name; (void)area; }

static int lv_epic_setup_bg_and_output_layer(EPIC_LayerConfigTypeDef *bg,
                                            EPIC_LayerConfigTypeDef *output,
                                            lv_draw_task_t *task, const lv_area_t *area)
{
    (void)task;
    memset(bg, 0, sizeof(*bg));
    memset(output, 0, sizeof(*output));
    output->width = (uint32_t)lv_area_get_width(area);
    output->height = (uint32_t)lv_area_get_height(area);
    return 0;
}

static void lv_draw_epic_border(lv_draw_task_t *task, const lv_draw_border_dsc_t *dsc,
                                const lv_area_t *area)
{ (void)task; (void)dsc; (void)area; }
static void lv_draw_epic_fill(lv_draw_task_t *task, const lv_draw_fill_dsc_t *dsc,
                              const lv_area_t *area)
{ (void)task; (void)dsc; (void)area; }
static void lv_draw_epic_img(lv_draw_task_t *task, const lv_draw_image_dsc_t *dsc,
                             const lv_area_t *area)
{ (void)task; (void)dsc; (void)area; }

#include "epic_label_under_test.inc"

int test_epic_glyph(lv_font_glyph_dsc_t *glyph)
{
    lv_draw_glyph_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    lv_area_t coords = {0, 0, glyph->box_w - 1, glyph->box_h - 1};
    dsc.g = glyph;
    dsc.format = glyph->format;
    dsc.letter_coords = &coords;
    dsc.opa = LV_OPA_COVER;
    dsc.color = lv_color_white();
    unsigned before = pixel_errors;
    _draw_epic_letter(NULL, &dsc, NULL, NULL);
    lv_font_glyph_release_draw_data(glyph);
    return pixel_errors == before ? 0 : 1;
}

int test_epic_result(void)
{
    printf("EPIC pixels: submissions=%u mismatches=%u\n", submissions, pixel_errors);
    drv_epic_cont_blend_reset();
    return pixel_errors == 0 && submissions > 100 ? 0 : 1;
}
