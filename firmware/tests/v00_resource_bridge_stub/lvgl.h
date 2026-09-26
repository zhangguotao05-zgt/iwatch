#ifndef V00_RESOURCE_BRIDGE_LVGL_STUB_H
#define V00_RESOURCE_BRIDGE_LVGL_STUB_H

#include <stdint.h>

#ifdef _MSC_VER
#define ALIGN(bytes) __declspec(align(bytes))
#define SECTION(name)
#else
#define ALIGN(bytes) __attribute__((aligned(bytes)))
#define SECTION(name) __attribute__((section(name)))
#endif

#define LV_COLOR_FORMAT_RAW 1
#define LV_COLOR_FORMAT_RAW_ALPHA 2
#define LV_IMAGE_FLAGS_USER1 0x0100

typedef struct {
    uint8_t magic;
    uint8_t cf;
    uint16_t flags;
    uint16_t w;
    uint16_t h;
    uint16_t stride;
    uint16_t reserved_2;
} lv_image_header_t;

typedef struct {
    lv_image_header_t header;
    uint32_t data_size;
    const uint8_t *data;
    const void *reserved;
    const void *reserved_2;
} lv_image_dsc_t;

_Static_assert(sizeof(lv_image_header_t) == 12u, "LVGL 图像头大小变化");
_Static_assert(sizeof(lv_image_dsc_t) == 16u + 3u * sizeof(void *),
               "LVGL 图像描述符大小变化");

#endif
