#include "lvgl.h"
#include "src/draw/lv_draw_label_private.h"
#include "src/draw/lv_image_decoder_private.h"
#include "src/draw/lv_draw_private.h"
#include "iw_gui_owner.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 硬件边界使用独立的 CPU/GPU 视图，只有正确的缓存清理范围才能同步像素。 */
#define LV_USE_DRAW_EPIC 1
#define LV_IMAGE_FLAGS_EZIP LV_IMAGE_FLAGS_USER1
#define LV_IMAGE_FLAGS_JPEG LV_IMAGE_FLAGS_USER2
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
#define EPIC_INPUT_L8 9
#define EPIC_INPUT_EZIP 10
#define EPIC_INPUT_JPEG 11
#define EPIC_INPUT_SCALE_NONE 1
#define ALPHA_BLEND_OVERWRITE 2

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
    uint8_t *lookup_table;
    struct { int32_t angle, pivot_x, pivot_y; uint32_t scale_x, scale_y; } transform_cfg;
} EPIC_LayerConfigTypeDef;

static struct {
    EPIC_HandleTypeDef epic_handle;
    bool cont_mode;
} drv_epic;
static uint8_t gpu_pixels[128 * 128];
static const uint8_t *clean_address;
static uint32_t clean_size;
static unsigned pixel_errors, submissions;
static void (*epic_error_cb)(void);
static int image_blend_result;
static unsigned image_faults;
static int letter_blend_result;
static unsigned letter_faults;

static void image_fault(void) { image_faults++; iw_gui_fault_raise(); }
static void letter_fault(void) { letter_faults++; }
static int lv_img_2_epic_cf(lv_color_format_t format) { (void)format; return EPIC_INPUT_A8; }
static int drv_epic_blend(EPIC_LayerConfigTypeDef *input, uint8_t count,
                          EPIC_LayerConfigTypeDef *output, void *callback)
{
    (void)input; (void)count; (void)output; (void)callback;
    return image_blend_result;
}

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
    if(letter_blend_result != RT_EOK) return letter_blend_result;
    return gpu_consume(fg);
}

static int HAL_EPIC_ContBlendRepeat(EPIC_HandleTypeDef *handle, EPIC_LayerConfigTypeDef *fg,
                                  EPIC_LayerConfigTypeDef *mask, EPIC_LayerConfigTypeDef *output)
{
    (void)handle; (void)mask; (void)output;
    if(letter_blend_result != RT_EOK) return letter_blend_result;
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

bool lv_draw_epic_report_error(void);

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
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4100 4189)
#endif
#include "epic_image_set_error_under_test.inc"
#include "epic_image_report_error_under_test.inc"
#include "epic_image_core_under_test.inc"
#include "ezip_decoder_under_test.inc"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

void test_font_arm_failure(size_t index);
void test_font_owner(bool owner, bool idle);
size_t test_font_live_blocks(void);
unsigned test_font_assert_count(void);

int test_epic_image_faults(void)
{
    static const uint8_t pixels[16] = {0};
    unsigned before_faults = image_faults;
    unsigned before_asserts = test_font_assert_count();
    size_t before_blocks = test_font_live_blocks();
    lv_draw_buf_t decoded = {0};
    lv_image_decoder_dsc_t decoder = {0};
    lv_draw_image_dsc_t draw = {0};
    lv_draw_task_t task = {0};
    lv_area_t area = {0, 0, 3, 3};

    lv_draw_epic_set_error_cb(image_fault);
    decoded.header.w = 4;
    decoded.header.h = 4;
    decoded.header.flags = LV_IMAGE_FLAGS_EZIP;
    decoded.data = (uint8_t *)pixels;
    decoded.data_size = sizeof(pixels);
    decoder.decoded = &decoded;
    draw.scale_x = LV_SCALE_NONE;
    draw.scale_y = LV_SCALE_NONE;
    draw.opa = LV_OPA_COVER;
    image_blend_result = RT_ERROR;
    img_draw_core(&task, &draw, &decoder, NULL, &area, &area);
    if(image_faults != before_faults + 1u || test_font_assert_count() != before_asserts ||
       !iw_gui_fault_pending()) return 1;
    test_font_owner(true, false);
    if(!iw_gui_fault_process() || !iw_gui_fault_pending()) return 6;
    test_font_owner(true, true);
    if(iw_gui_fault_process() || iw_gui_fault_pending()) return 7;
    image_blend_result = RT_EOK;
    img_draw_core(&task, &draw, &decoder, NULL, &area, &area);
    if(image_faults != before_faults + 1u) return 2;

    lv_image_dsc_t image = {0};
    image.header.flags = LV_IMAGE_FLAGS_EZIP;
    image.data = pixels;
    image.data_size = sizeof(pixels);
    decoder = (lv_image_decoder_dsc_t){0};
    decoder.src_type = LV_IMAGE_SRC_VARIABLE;
    decoder.src = &image;
    decoder.header = image.header;
    test_font_arm_failure(1);
    if(decoder_open(NULL, &decoder) != LV_RESULT_INVALID || decoder.decoded ||
       image_faults != before_faults + 2u || test_font_assert_count() != before_asserts ||
       test_font_live_blocks() != before_blocks) return 3;
    if(iw_gui_fault_process() || iw_gui_fault_pending()) return 8;
    test_font_arm_failure(0);
    if(decoder_open(NULL, &decoder) != LV_RESULT_OK || !decoder.decoded) return 4;
    lv_free((void *)decoder.decoded);
    lv_draw_epic_set_error_cb(NULL);
    return test_font_live_blocks() == before_blocks ? 0 : 5;
}

int test_epic_glyph(lv_font_glyph_dsc_t *glyph)
{
    static bool failure_tested;
    lv_draw_glyph_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    lv_area_t coords = {0, 0, glyph->box_w - 1, glyph->box_h - 1};
    dsc.g = glyph;
    dsc.format = glyph->format;
    dsc.letter_coords = &coords;
    dsc.opa = LV_OPA_COVER;
    dsc.color = lv_color_white();
    unsigned before = pixel_errors;
    if(!failure_tested && glyph->box_w && glyph->box_h) {
        unsigned faults_before = letter_faults;
        unsigned asserts_before = test_font_assert_count();
        drv_epic_cont_blend_reset();
        lv_draw_epic_set_error_cb(letter_fault);
        letter_blend_result = RT_ERROR;
        _draw_epic_letter(NULL, &dsc, NULL, NULL);
        letter_blend_result = RT_EOK;
        lv_draw_epic_set_error_cb(NULL);
        drv_epic_cont_blend_reset();
        if(letter_faults != faults_before + 1u ||
           test_font_assert_count() != asserts_before || dsc._draw_buf) {
            lv_font_glyph_release_draw_data(glyph);
            return 2;
        }
        failure_tested = true;
        lv_font_glyph_release_draw_data(glyph);
        return 0;
    }
    _draw_epic_letter(NULL, &dsc, NULL, NULL);
    lv_font_glyph_release_draw_data(glyph);
    return pixel_errors == before ? 0 : 1;
}

int test_epic_result(void)
{
    printf("EPIC pixels: submissions=%u mismatches=%u fault_paths=%u\n",
           submissions, pixel_errors, letter_faults);
    drv_epic_cont_blend_reset();
    return pixel_errors == 0 && submissions > 100 && letter_faults == 1 ? 0 : 1;
}

unsigned test_epic_submissions(void)
{
    return submissions;
}
