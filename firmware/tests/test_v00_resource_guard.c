#include "iw_v00_resource_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_guard(void)
{
    static const uint8_t data[] = "123456789";
    iw_v00_resource_meta_t meta = {
        .version = IW_V00_RESOURCE_ABI,
        .width = 3, .height = 1,
        .rgb_stride_bytes = 6, .alpha_stride_bytes = 3,
        .format = IW_V00_RESOURCE_RAW_RGB565A8,
        .release_allowed = false,
        .payload_bytes = 9,
        .crc32 = 0xcbf43926u
    };
    uint8_t corrupt[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '0'};
    for (unsigned i = 0; i < 1000u; ++i)
        assert(iw_v00_resource_validate(&meta, data, 9, false) == IW_V00_RESOURCE_OK);
    assert(iw_v00_resource_validate(&meta, corrupt, 9, false) == IW_V00_RESOURCE_INTEGRITY);
    assert(iw_v00_resource_validate(&meta, data, 8, false) == IW_V00_RESOURCE_SIZE);
    assert(iw_v00_resource_validate(&meta, NULL, 9, false) == IW_V00_RESOURCE_INVALID);
    assert(iw_v00_resource_validate(NULL, data, 9, false) == IW_V00_RESOURCE_INVALID);
    assert(iw_v00_resource_validate(&meta, data, 9, true) == IW_V00_RESOURCE_NOT_PUBLISHABLE);
    meta.version++;
    assert(iw_v00_resource_validate(&meta, data, 9, false) == IW_V00_RESOURCE_VERSION);
    meta.version--;
    meta.width = 391;
    assert(iw_v00_resource_validate(&meta, data, 9, false) == IW_V00_RESOURCE_GEOMETRY);
    meta.width = 3;
    meta.alpha_stride_bytes = 6;
    assert(iw_v00_resource_validate(&meta, data, 9, false) == IW_V00_RESOURCE_GEOMETRY);
    meta.alpha_stride_bytes = 3;
    meta.format = IW_V00_RESOURCE_RAW_RGB565A8;
    assert(iw_v00_resource_validate(&meta, data, 9, false) == IW_V00_RESOURCE_OK);
    meta.format = IW_V00_RESOURCE_RAW_RGB565;
    assert(iw_v00_resource_validate(&meta, data, 9, false) == IW_V00_RESOURCE_GEOMETRY);
    meta.alpha_stride_bytes = 0;
    assert(iw_v00_resource_validate(&meta, data, 9, false) == IW_V00_RESOURCE_SIZE);
}

static void test_binding(void)
{
    static const uint8_t data[] = {
        0, 0, 0, 16, 0x1c, 0x18, 0x20, 0, 0, 3, 0, 1, 0, 0, 0, 0
    };
    static const uint8_t other[] = {
        0, 0, 0, 16, 0x1c, 0x18, 0x20, 0, 0, 3, 0, 1, 0, 0, 0, 0
    };
    iw_v00_resource_binding_t binding = {
        .meta = {
            .version = IW_V00_RESOURCE_ABI,
            .width = 3, .height = 1,
            .rgb_stride_bytes = 6, .alpha_stride_bytes = 3,
            .format = IW_V00_RESOURCE_EZIP_RGB565A8,
            .ezip_codec = 0x1c, .ezip_flags = 0x18,
            .release_allowed = false,
            .payload_bytes = 16, .crc32 = 0x064e5881u
        },
        .payload = data
    };
    iw_v00_resource_image_t image = {
        .width = 3, .height = 1, .stride_bytes = 0,
        .color_format = 2, .data_size = 16, .data = data
    };

    assert(iw_v00_resource_validate_binding(&binding, &image, false) == IW_V00_RESOURCE_OK);
    assert(iw_v00_resource_validate_binding(&binding, &image, true) ==
           IW_V00_RESOURCE_NOT_PUBLISHABLE);
    image.data = other;
    assert(iw_v00_resource_validate_binding(&binding, &image, false) == IW_V00_RESOURCE_BINDING);
    image.data = data;
    image.width = 1;
    assert(iw_v00_resource_validate_binding(&binding, &image, false) == IW_V00_RESOURCE_BINDING);
    image.width = 3;
    image.color_format = 1;
    assert(iw_v00_resource_validate_binding(&binding, &image, false) == IW_V00_RESOURCE_BINDING);
    image.color_format = 2;
    image.data_size = 15;
    assert(iw_v00_resource_validate_binding(&binding, &image, false) == IW_V00_RESOURCE_BINDING);
    image.data_size = 16;
    binding.meta.crc32++;
    assert(iw_v00_resource_validate_binding(&binding, &image, false) == IW_V00_RESOURCE_INTEGRITY);
    assert(iw_v00_resource_validate_binding(NULL, &image, false) == IW_V00_RESOURCE_INVALID);
}

static void test_ezip_header(void)
{
    static const uint8_t data[] = {
        0, 0, 0, 16, 0x1c, 0x18, 0x20, 0, 0, 3, 0, 1, 0, 0, 0, 0
    };
    iw_v00_resource_meta_t meta = {
        .version = IW_V00_RESOURCE_ABI,
        .width = 3, .height = 1,
        .rgb_stride_bytes = 6, .alpha_stride_bytes = 3,
        .format = IW_V00_RESOURCE_EZIP_RGB565A8,
        .ezip_codec = 0x1c, .ezip_flags = 0x18,
        .payload_bytes = 16, .crc32 = 0x064e5881u
    };
    uint8_t bad[sizeof(data)];

    assert(iw_v00_resource_validate(&meta, data, sizeof(data), false) == IW_V00_RESOURCE_OK);
    memcpy(bad, data, sizeof(data));
    bad[3] = 15;
    assert(iw_v00_resource_validate(&meta, bad, sizeof(bad), false) == IW_V00_RESOURCE_SIZE);
    memcpy(bad, data, sizeof(data));
    bad[9] = 1;
    assert(iw_v00_resource_validate(&meta, bad, sizeof(bad), false) == IW_V00_RESOURCE_GEOMETRY);
    memcpy(bad, data, sizeof(data));
    bad[4] = 0x18;
    assert(iw_v00_resource_validate(&meta, bad, sizeof(bad), false) == IW_V00_RESOURCE_GEOMETRY);
    meta.ezip_codec = 0x13;
    assert(iw_v00_resource_validate(&meta, data, sizeof(data), false) == IW_V00_RESOURCE_GEOMETRY);
    meta.ezip_codec = 0x1c;
    meta.height = 2;
    assert(iw_v00_resource_validate(&meta, data, sizeof(data), false) == IW_V00_RESOURCE_GEOMETRY);

    static const uint8_t palette[] = {
        0, 0, 0, 16, 0x13, 0x08, 0x20, 0, 0, 3, 0, 1, 0, 0, 0, 0
    };
    meta.height = 1;
    meta.ezip_codec = 0x13;
    meta.ezip_flags = 0x08;
    meta.crc32 = 0xda2c033bu;
    assert(iw_v00_resource_validate(&meta, palette, sizeof(palette), false) ==
           IW_V00_RESOURCE_GEOMETRY);
    meta.ezip_flags = 0x18;
    assert(iw_v00_resource_validate(&meta, palette, sizeof(palette), false) ==
           IW_V00_RESOURCE_GEOMETRY);
}

int main(void)
{
    test_guard();
    test_binding();
    test_ezip_header();
    puts("V00 resource guard OK");
    return 0;
}
