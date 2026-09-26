#include "lvgl.h"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPEC_WIDTH 390u
#define SPEC_HEIGHT 450u
#define SPEC_BASELINE 220

typedef struct {
    const char *role;
    const char *text;
    int32_t size_px;
    int32_t tracking_px;
} specimen_t;

/* 每项对应设计交接的一个边界样例；固件正式映射仍由签字后的 JSON 决定。 */
static const specimen_t specimens[] = {
    {"FACE_TIME", "00:00", 84, -4}, {"FACE_TIME", "01:11", 84, -4},
    {"FACE_TIME", "08:08", 84, -4}, {"FACE_TIME", "10:09", 84, -4},
    {"FACE_TIME", "23:59", 84, -4},
    {"TIMER_PRESET", "1", 50, 0}, {"TIMER_PRESET", "3", 50, 0},
    {"TIMER_PRESET", "5", 50, 0}, {"TIMER_PRESET", "10", 50, 0},
    {"TIMER_PRESET", "15", 50, 0}, {"TIMER_PRESET", "30", 50, 0},
    {"TIMER_SECTION", "所有计时器", 25, 0},
    {"ALARM_VALUE", "00", 45, 0}, {"ALARM_VALUE", "06", 45, 0},
    {"ALARM_VALUE", "45", 45, 0}, {"ALARM_VALUE", "59", 45, 0},
    {"ROW_TITLE", "文字大小", 26, 0}, {"ROW_TITLE", "显示与亮度", 26, 0},
    {"ROW_TITLE", "全天候显示", 26, 0},
    {"CONTROL_BATTERY", "0%", 34, 0}, {"CONTROL_BATTERY", "9%", 34, 0},
    {"CONTROL_BATTERY", "96%", 34, 0}, {"CONTROL_BATTERY", "100%", 34, 0},
    {"CONTROL_BATTERY", "--%", 34, 0},
    {"ALARM_SEPARATOR", ":", 35, 0},
    {"ROW_DETAIL", "已打开", 20, 0}, {"ROW_DETAIL", "未接入", 20, 0},
    {"ACTION_LABEL", "自定义", 26, 0}, {"ACTION_LABEL", "取消", 26, 0},
    {"ACTION_LABEL", "确认", 26, 0},
    {"FACE_TIME", "12:47", 84, -4},
    {"CONTROL_BATTERY", "47%", 34, 0},
    {"FACE_WEEKDAY", "周一", 30, 0},
    {"ROW_DETAIL", "未知", 20, 0},
    {"PAGE_TITLE", "计时器", 26, 0},
};

static uint8_t pixels[SPEC_WIDTH * SPEC_HEIGHT * 2u];
static unsigned flush_count;

static uint32_t specimen_color(const char *name, uint32_t fallback)
{
    const char *text = getenv(name);
    if (!text || !*text) return fallback;
    char *end = NULL;
    unsigned long color = strtoul(text, &end, 16);
    if (!end || *end || color > 0xFFFFFFul) return fallback;
    return (uint32_t)color;
}

static int32_t specimen_integer(const char *name, int32_t fallback, int32_t lower, int32_t upper)
{
    const char *text = getenv(name);
    if (!text || !*text) return fallback;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!end || *end || value < lower || value > upper) return fallback;
    return (int32_t)value;
}

static void specimen_flush(lv_display_t *display, const lv_area_t *area, uint8_t *buffer)
{
    (void)area;
    (void)buffer;
    flush_count++;
    lv_display_flush_ready(display);
}

int test_v00_typography(lv_display_t *display, const void *data, size_t size, size_t index)
{
    if (index >= sizeof(specimens) / sizeof(specimens[0])) return 64;
    const specimen_t *spec = &specimens[index];
    int32_t actual_size = spec->size_px;
    const char *override = getenv("V00_SPEC_SIZE");
    if (override && *override) {
        char *end = NULL;
        long parsed = strtol(override, &end, 10);
        if (!end || *end || parsed < 10 || parsed > 120) return 65;
        actual_size = (int32_t)parsed;
    }
    lv_font_t *font = lv_tiny_ttf_create_data_ex(data, size, actual_size,
                                                LV_FONT_KERNING_NORMAL, 16);
    assert(font);
    lv_obj_t *screen = lv_display_get_screen_active(display);
    const uint32_t foreground = specimen_color("V00_SPEC_FG", 0xFFFFFFu);
    const uint32_t background = specimen_color("V00_SPEC_BG", 0x000000u);
    lv_obj_set_style_bg_color(screen, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_t *label = lv_label_create(screen);
    assert(label);
    lv_obj_remove_style_all(label);
    const int32_t label_x = specimen_integer("V00_PAGE_X", 0, 0, 389);
    const int32_t label_width = specimen_integer("V00_PAGE_W", SPEC_WIDTH, 1, 390);
    const int32_t label_height = specimen_integer("V00_PAGE_H", 140, 1, 450);
    const int32_t baseline = specimen_integer("V00_PAGE_BASELINE", SPEC_BASELINE, 0, 449);
    lv_obj_set_size(label, label_width, label_height);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(label, lv_color_hex(foreground), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_letter_space(label, spec->tracking_px, 0);
    const char *align = getenv("V00_PAGE_ALIGN");
    lv_obj_set_style_text_align(label,
        align && strcmp(align, "left") == 0 ? LV_TEXT_ALIGN_LEFT :
        align && strcmp(align, "right") == 0 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, spec->text);
    const int32_t label_y = baseline - (font->line_height - font->base_line);
    lv_obj_set_pos(label, label_x, label_y);

    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, specimen_flush);
    memset(pixels, 0, sizeof(pixels));
    lv_obj_invalidate(screen);
    lv_refr_now(display);
    assert(flush_count && !lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN));

    unsigned left = SPEC_WIDTH, top = SPEC_HEIGHT, right = 0, bottom = 0, count = 0;
    for (unsigned y = 0; y < SPEC_HEIGHT; y++) {
        for (unsigned x = 0; x < SPEC_WIDTH; x++) {
            uint16_t color;
            memcpy(&color, pixels + 2u * (y * SPEC_WIDTH + x), sizeof(color));
            /* 近白文字沿用亮色阈值；彩色字按前景/背景对比统计有效像素。 */
            if (((foreground >> 16) & 255u) >= 170u &&
                ((foreground >> 8) & 255u) >= 170u && (foreground & 255u) >= 170u) {
                if (((color >> 11) & 31u) < 21u || ((color >> 5) & 63u) < 42u ||
                    (color & 31u) < 21u) continue;
            } else {
                int pr = (int)(((color >> 11) & 31u) * 255u / 31u);
                int pg = (int)(((color >> 5) & 63u) * 255u / 63u);
                int pb = (int)((color & 31u) * 255u / 31u);
                int br = (int)((background >> 16) & 255u);
                int bg = (int)((background >> 8) & 255u);
                int bb = (int)(background & 255u);
                int fr = (int)((foreground >> 16) & 255u);
                int fg = (int)((foreground >> 8) & 255u);
                int fb = (int)(foreground & 255u);
                int observed = abs(pr - br);
                int expected = abs(fr - br);
                if (abs(pg - bg) > observed) observed = abs(pg - bg);
                if (abs(pb - bb) > observed) observed = abs(pb - bb);
                if (abs(fg - bg) > expected) expected = abs(fg - bg);
                if (abs(fb - bb) > expected) expected = abs(fb - bb);
                if (observed * 100 < expected * 65) continue;
            }
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
            count++;
        }
    }
    assert(count && left < SPEC_WIDTH && top < SPEC_HEIGHT);
    const char *directory = getenv("V00_SPEC_OUTPUT");
    if (!directory || !*directory) directory = "work/v00/type-specimens/raw";
    char path[240];
    snprintf(path, sizeof(path), "%s/%02u.rgb565", directory, (unsigned)index);
    FILE *file = NULL;
#ifdef _WIN32
    assert(fopen_s(&file, path, "wb") == 0);
#else
    file = fopen(path, "wb");
#endif
    assert(file && fwrite(pixels, 1, sizeof(pixels), file) == sizeof(pixels));
    fclose(file);
    printf("V00_FONT role=%s index=%zu text=%s size=%ld tracking=%ld baseline=%ld line_height=%d base_line=%d fg=%06X bg=%06X "
           "container=%ld,%ld,%ld,%ld ink=%u,%u,%u,%u pixels=%u result=ok\n",
           spec->role, index, spec->text, (long)actual_size, (long)spec->tracking_px,
           (long)baseline, font->line_height, font->base_line, foreground, background,
           (long)label_x, (long)label_y, (long)label_width, (long)label_height,
           left, top, right - left + 1u, bottom - top + 1u, count);
    lv_obj_delete(label);
    lv_tiny_ttf_destroy(font);
    return 0;
}
