/* 保留既有分配器和断言统计；旧矩阵入口不执行。 */
#define main legacy_test_main
#include "test_tiny_ttf_oom.c"
#undef main
#include <assert.h>
extern lv_display_t *dynamic_target_init(const void *, size_t);
extern int dynamic_router_case(lv_display_t *, unsigned);
int main(int argc, char **argv)
{
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 3) return 64;
    unsigned mode = (unsigned)strtoul(argv[2], NULL, 10);
    if (mode > 10) return 65;
    size_t font_size = 0;
    uint8_t *font_data = load_file(argv[1], &font_size);
    if (!font_data) return 66;
    lv_init();
    lv_tiny_ttf_set_oom_cb(oom_callback, NULL);
    lv_display_t *display = dynamic_target_init(font_data, font_size);
    int result = dynamic_router_case(display, mode);
    assert(assert_count == 0 && oom_count == 0);
    lv_tiny_ttf_set_oom_cb(NULL, NULL);
    /* 显式销毁由路由用例完成，字库必须活到 LVGL 完成反初始化。 */
    lv_deinit();
    free(font_data);
    return result;
}
