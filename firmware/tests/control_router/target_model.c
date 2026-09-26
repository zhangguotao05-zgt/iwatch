/* 原样复用 TARGET 硬件夹具，仅在释放边界增加计数与日志。 */
#define app_cache_free original_cache_free
#include "../v00_target_view/test_target_view.c"
#undef app_cache_free

static size_t release_calls;
void app_cache_free(void *pixels)
{
    original_cache_free(pixels);
    ++release_calls;
    printf("CACHE free=%p live=%zu peak=%zu releases=%zu\n", pixels, live_bytes, peak_bytes, release_calls);
}
void dynamic_cache_stats(size_t *live, size_t *peak, size_t *frees)
{
    *live = live_bytes;
    *peak = peak_bytes;
    *frees = release_calls;
}
lv_display_t *dynamic_target_init(const void *data, size_t size)
{
    assert(iw_font_init(data, (uint32_t)size));
    assert(test_v00_fonts_load() && test_v00_assets_load());
    /* 本用例不刷新像素；解码器仍按原夹具注册，不伪称硬件绘制。 */
    for (unsigned i = 0; i < 37; ++i) {
        assert(iw_v00_resource_catalog_validate(i, false) == IW_V00_RESOURCE_OK);
        const lv_image_dsc_t *raw = test_v00_assets_images()[i];
        assert(lv_draw_buf_init(&decoded[i], raw->header.w, raw->header.h,
            LV_COLOR_FORMAT_RGB565A8, raw->header.stride, (void *)raw->data, raw->data_size) == LV_RESULT_OK);
    }
    lv_image_decoder_t *decoder = lv_image_decoder_create();
    assert(decoder);
    lv_image_decoder_set_info_cb(decoder, mock_info);
    lv_image_decoder_set_open_cb(decoder, mock_open);
    lv_image_decoder_set_close_cb(decoder, mock_close);
    lv_display_t *display = lv_display_create(390, 450);
    assert(display);
    return display;
}
