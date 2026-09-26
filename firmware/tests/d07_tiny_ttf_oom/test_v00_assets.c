#include "test_v00_assets.h"
#include "iw_product_scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *file;
    uint16_t width, height;
} asset_spec_t;

/* 顺序固定为设计包 manifest.resources；只供主机样片使用。 */
static const asset_spec_t specs[IW_ICON_V00_ASSET_COUNT] = {
    {"ICt_Phone_AppIcon-64x64-1cc081", 64, 64},
    {"ICt_Mail_AppIcon-71x71-8651bc", 71, 71},
    {"ICt_Messages_AppIcon-64x64-f0d6c7", 64, 64},
    {"ICt_Mindfulness_AppIcon-65x65-dffccf", 65, 65},
    {"ICt_Activity_AppIcon-83x83-2fd12f", 83, 83},
    {"ICt_Workout_AppIcon-83x83-d22dbc", 83, 83},
    {"ICt_Settings_AppIcon-65x65-937ae8", 65, 65},
    {"ICt_Timers_AppIcon-80x80-bee344", 80, 80},
    {"ICt_CalendarAppIcon-91x91-9b78bd", 91, 91},
    {"ICt_Stopwatch_AppIcon-80x80-fa169d", 80, 80},
    {"ICt_WorldClock_AppIcon-67x67-5f14f8", 67, 67},
    {"ICt_Maps_AppIcon-79x79-b6b7ac", 79, 79},
    {"ICt_Weather_AppIcon-79x79-f3e41a", 79, 79},
    {"ICt_Alarms_AppIcon-67x67-f2d14f", 67, 67},
    {"ICt_Compass_AppIcon-64x64-13980a", 64, 64},
    {"ICt_Photos_AppIcon-78x78-257cb6", 78, 78},
    {"ICt_Sleep_AppIcon-64x64-944c3e", 64, 64},
    {"music-59x59-4d73be", 59, 59},
    {"restore-25x25-e7a179", 25, 25},
    {"decoration-92x92-f49b0e", 92, 92},
    {"flower-65x65-e07948", 65, 65},
    {"run-61x61-9c588c", 61, 61},
    {"close-26x26-6150ef", 26, 26},
    {"back-26x26-e153fd", 26, 26},
    {"decoration-333x333-f15119", 333, 333},
    {"close-32x32-f30976", 32, 32},
    {"check-36x36-fb2c9e", 36, 36},
    {"sun-28x28-40e472", 28, 28},
    {"sun-43x43-addcf2", 43, 43},
    {"ICt_connectediPhone-24x24-acc9cd", 24, 24},
    {"ICt_silentMode-24x24-cdf64a", 24, 24},
    {"ICt_statusDNDFocus-24x24-20a117", 24, 24},
    {"ICt_Wifi-58x58-0ecb11", 58, 58},
    {"ICt_airplaneMode-58x58-99f564", 58, 58},
    {"ICt_pingPhone-58x58-78008a", 58, 58},
    {"ICt_flashlight-58x58-18dfd7", 58, 58},
    {"ICt_doNotDisturb-58x58-e14f6c", 58, 58},
    {"control-background-only", 390, 450}
};

static uint8_t *pixels[IW_ICON_V00_ASSET_COUNT];
static lv_image_dsc_t images[IW_ICON_V00_ASSET_COUNT];
static const lv_image_dsc_t *image_refs[IW_ICON_V00_ASSET_COUNT];

void test_v00_assets_release(void)
{
    for (unsigned i = 0; i < IW_ICON_V00_ASSET_COUNT; i++) {
        free(pixels[i]);
        pixels[i] = NULL;
        image_refs[i] = NULL;
        memset(&images[i], 0, sizeof(images[i]));
    }
}

bool test_v00_assets_load(void)
{
    const char *root = getenv("IW_V00_ASSET_ROOT");
    if (!root || !root[0]) root = "docs/assets/v00-design-handoff/assets/raw";
    if (image_refs[0]) return true;
    for (unsigned i = 0; i < IW_ICON_V00_ASSET_COUNT; i++) {
        char path[1024];
        bool opaque = i == IW_ICON_V00_ASSET_COUNT - 1u;
        int count = snprintf(path, sizeof(path), "%s/%s.%s.bin", root,
                             specs[i].file, opaque ? "rgb565" : "rgb565a8");
        if (count < 0 || (size_t)count >= sizeof(path)) goto failed;
        FILE *file = NULL;
#ifdef _MSC_VER
        if (fopen_s(&file, path, "rb") != 0) goto failed;
#else
        file = fopen(path, "rb");
        if (!file) goto failed;
#endif
        size_t bytes = (size_t)specs[i].width * specs[i].height * (opaque ? 2u : 3u);
        pixels[i] = malloc(bytes);
        if (!pixels[i]) { fclose(file); goto failed; }
        bool valid = fread(pixels[i], 1u, bytes, file) == bytes && fgetc(file) == EOF;
        fclose(file);
        if (!valid) goto failed;
        images[i] = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC,
                       .cf = opaque ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_RGB565A8,
                       .w = specs[i].width, .h = specs[i].height,
                       .stride = (uint16_t)(specs[i].width * 2u)},
            .data_size = (uint32_t)bytes, .data = pixels[i]
        };
        image_refs[i] = &images[i];
    }
    return true;
failed:
    test_v00_assets_release();
    return false;
}

const lv_image_dsc_t *const *test_v00_assets_images(void)
{
    return image_refs;
}
