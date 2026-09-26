/* 本文件由 build_v00_resource_bridge.py 生成，不要手改。 */
#include "iw_v00_resource_catalog.h"
#include "lvgl.h"

typedef struct {
    const char *id;
    const lv_image_dsc_t *image;
    iw_v00_resource_binding_t binding;
} catalog_entry_t;

_Static_assert(LV_COLOR_FORMAT_RAW == 1, "LVGL RAW 格式已变化");
_Static_assert(LV_COLOR_FORMAT_RAW_ALPHA == 2, "LVGL RAW_ALPHA 格式已变化");

extern const lv_image_dsc_t v00_ICt_Phone_AppIcon_64x64_1cc081;
extern const uint8_t v00_ICt_Phone_AppIcon_64x64_1cc081_map[];
extern const lv_image_dsc_t v00_ICt_Mail_AppIcon_71x71_8651bc;
extern const uint8_t v00_ICt_Mail_AppIcon_71x71_8651bc_map[];
extern const lv_image_dsc_t v00_ICt_Messages_AppIcon_64x64_f0d6c7;
extern const uint8_t v00_ICt_Messages_AppIcon_64x64_f0d6c7_map[];
extern const lv_image_dsc_t v00_ICt_Mindfulness_AppIcon_65x65_dffccf;
extern const uint8_t v00_ICt_Mindfulness_AppIcon_65x65_dffccf_map[];
extern const lv_image_dsc_t v00_ICt_Activity_AppIcon_83x83_2fd12f;
extern const uint8_t v00_ICt_Activity_AppIcon_83x83_2fd12f_map[];
extern const lv_image_dsc_t v00_ICt_Workout_AppIcon_83x83_d22dbc;
extern const uint8_t v00_ICt_Workout_AppIcon_83x83_d22dbc_map[];
extern const lv_image_dsc_t v00_ICt_Settings_AppIcon_65x65_937ae8;
extern const uint8_t v00_ICt_Settings_AppIcon_65x65_937ae8_map[];
extern const lv_image_dsc_t v00_ICt_Timers_AppIcon_80x80_bee344;
extern const uint8_t v00_ICt_Timers_AppIcon_80x80_bee344_map[];
extern const lv_image_dsc_t v00_ICt_CalendarAppIcon_91x91_9b78bd;
extern const uint8_t v00_ICt_CalendarAppIcon_91x91_9b78bd_map[];
extern const lv_image_dsc_t v00_ICt_Stopwatch_AppIcon_80x80_fa169d;
extern const uint8_t v00_ICt_Stopwatch_AppIcon_80x80_fa169d_map[];
extern const lv_image_dsc_t v00_ICt_WorldClock_AppIcon_67x67_5f14f8;
extern const uint8_t v00_ICt_WorldClock_AppIcon_67x67_5f14f8_map[];
extern const lv_image_dsc_t v00_ICt_Maps_AppIcon_79x79_b6b7ac;
extern const uint8_t v00_ICt_Maps_AppIcon_79x79_b6b7ac_map[];
extern const lv_image_dsc_t v00_ICt_Weather_AppIcon_79x79_f3e41a;
extern const uint8_t v00_ICt_Weather_AppIcon_79x79_f3e41a_map[];
extern const lv_image_dsc_t v00_ICt_Alarms_AppIcon_67x67_f2d14f;
extern const uint8_t v00_ICt_Alarms_AppIcon_67x67_f2d14f_map[];
extern const lv_image_dsc_t v00_ICt_Compass_AppIcon_64x64_13980a;
extern const uint8_t v00_ICt_Compass_AppIcon_64x64_13980a_map[];
extern const lv_image_dsc_t v00_ICt_Photos_AppIcon_78x78_257cb6;
extern const uint8_t v00_ICt_Photos_AppIcon_78x78_257cb6_map[];
extern const lv_image_dsc_t v00_ICt_Sleep_AppIcon_64x64_944c3e;
extern const uint8_t v00_ICt_Sleep_AppIcon_64x64_944c3e_map[];
extern const lv_image_dsc_t v00_music_59x59_4d73be;
extern const uint8_t v00_music_59x59_4d73be_map[];
extern const lv_image_dsc_t v00_restore_25x25_e7a179;
extern const uint8_t v00_restore_25x25_e7a179_map[];
extern const lv_image_dsc_t v00_decoration_92x92_f49b0e;
extern const uint8_t v00_decoration_92x92_f49b0e_map[];
extern const lv_image_dsc_t v00_flower_65x65_e07948;
extern const uint8_t v00_flower_65x65_e07948_map[];
extern const lv_image_dsc_t v00_run_61x61_9c588c;
extern const uint8_t v00_run_61x61_9c588c_map[];
extern const lv_image_dsc_t v00_close_26x26_6150ef;
extern const uint8_t v00_close_26x26_6150ef_map[];
extern const lv_image_dsc_t v00_back_26x26_e153fd;
extern const uint8_t v00_back_26x26_e153fd_map[];
extern const lv_image_dsc_t v00_decoration_333x333_f15119;
extern const uint8_t v00_decoration_333x333_f15119_map[];
extern const lv_image_dsc_t v00_close_32x32_f30976;
extern const uint8_t v00_close_32x32_f30976_map[];
extern const lv_image_dsc_t v00_check_36x36_fb2c9e;
extern const uint8_t v00_check_36x36_fb2c9e_map[];
extern const lv_image_dsc_t v00_sun_28x28_40e472;
extern const uint8_t v00_sun_28x28_40e472_map[];
extern const lv_image_dsc_t v00_sun_43x43_addcf2;
extern const uint8_t v00_sun_43x43_addcf2_map[];
extern const lv_image_dsc_t v00_ICt_connectediPhone_24x24_acc9cd;
extern const uint8_t v00_ICt_connectediPhone_24x24_acc9cd_map[];
extern const lv_image_dsc_t v00_ICt_silentMode_24x24_cdf64a;
extern const uint8_t v00_ICt_silentMode_24x24_cdf64a_map[];
extern const lv_image_dsc_t v00_ICt_statusDNDFocus_24x24_20a117;
extern const uint8_t v00_ICt_statusDNDFocus_24x24_20a117_map[];
extern const lv_image_dsc_t v00_ICt_Wifi_58x58_0ecb11;
extern const uint8_t v00_ICt_Wifi_58x58_0ecb11_map[];
extern const lv_image_dsc_t v00_ICt_airplaneMode_58x58_99f564;
extern const uint8_t v00_ICt_airplaneMode_58x58_99f564_map[];
extern const lv_image_dsc_t v00_ICt_pingPhone_58x58_78008a;
extern const uint8_t v00_ICt_pingPhone_58x58_78008a_map[];
extern const lv_image_dsc_t v00_ICt_flashlight_58x58_18dfd7;
extern const uint8_t v00_ICt_flashlight_58x58_18dfd7_map[];
extern const lv_image_dsc_t v00_ICt_doNotDisturb_58x58_e14f6c;
extern const uint8_t v00_ICt_doNotDisturb_58x58_e14f6c_map[];

static const catalog_entry_t catalog[] = {
    { "ICt_Phone_AppIcon-64x64-1cc081", &v00_ICt_Phone_AppIcon_64x64_1cc081, {
        { IW_V00_RESOURCE_ABI, 64, 64, 128, 64, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 4367, 0x6ae4ae8cu },
        v00_ICt_Phone_AppIcon_64x64_1cc081_map
    } },
    { "ICt_Mail_AppIcon-71x71-8651bc", &v00_ICt_Mail_AppIcon_71x71_8651bc, {
        { IW_V00_RESOURCE_ABI, 71, 71, 142, 71, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 4996, 0xf53cff9bu },
        v00_ICt_Mail_AppIcon_71x71_8651bc_map
    } },
    { "ICt_Messages_AppIcon-64x64-f0d6c7", &v00_ICt_Messages_AppIcon_64x64_f0d6c7, {
        { IW_V00_RESOURCE_ABI, 64, 64, 128, 64, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 4248, 0x12f2d6ddu },
        v00_ICt_Messages_AppIcon_64x64_f0d6c7_map
    } },
    { "ICt_Mindfulness_AppIcon-65x65-dffccf", &v00_ICt_Mindfulness_AppIcon_65x65_dffccf, {
        { IW_V00_RESOURCE_ABI, 65, 65, 130, 65, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 5133, 0xb7152cd2u },
        v00_ICt_Mindfulness_AppIcon_65x65_dffccf_map
    } },
    { "ICt_Activity_AppIcon-83x83-2fd12f", &v00_ICt_Activity_AppIcon_83x83_2fd12f, {
        { IW_V00_RESOURCE_ABI, 83, 83, 166, 83, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 9627, 0x38237b25u },
        v00_ICt_Activity_AppIcon_83x83_2fd12f_map
    } },
    { "ICt_Workout_AppIcon-83x83-d22dbc", &v00_ICt_Workout_AppIcon_83x83_d22dbc, {
        { IW_V00_RESOURCE_ABI, 83, 83, 166, 83, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 7092, 0x3af50aa7u },
        v00_ICt_Workout_AppIcon_83x83_d22dbc_map
    } },
    { "ICt_Settings_AppIcon-65x65-937ae8", &v00_ICt_Settings_AppIcon_65x65_937ae8, {
        { IW_V00_RESOURCE_ABI, 65, 65, 130, 65, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 6600, 0x10722114u },
        v00_ICt_Settings_AppIcon_65x65_937ae8_map
    } },
    { "ICt_Timers_AppIcon-80x80-bee344", &v00_ICt_Timers_AppIcon_80x80_bee344, {
        { IW_V00_RESOURCE_ABI, 80, 80, 160, 80, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 7321, 0x72df2413u },
        v00_ICt_Timers_AppIcon_80x80_bee344_map
    } },
    { "ICt_CalendarAppIcon-91x91-9b78bd", &v00_ICt_CalendarAppIcon_91x91_9b78bd, {
        { IW_V00_RESOURCE_ABI, 91, 91, 182, 91, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 5848, 0x296a714du },
        v00_ICt_CalendarAppIcon_91x91_9b78bd_map
    } },
    { "ICt_Stopwatch_AppIcon-80x80-fa169d", &v00_ICt_Stopwatch_AppIcon_80x80_fa169d, {
        { IW_V00_RESOURCE_ABI, 80, 80, 160, 80, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 7286, 0x3484c9a4u },
        v00_ICt_Stopwatch_AppIcon_80x80_fa169d_map
    } },
    { "ICt_WorldClock_AppIcon-67x67-5f14f8", &v00_ICt_WorldClock_AppIcon_67x67_5f14f8, {
        { IW_V00_RESOURCE_ABI, 67, 67, 134, 67, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 6090, 0x01d3222du },
        v00_ICt_WorldClock_AppIcon_67x67_5f14f8_map
    } },
    { "ICt_Maps_AppIcon-79x79-b6b7ac", &v00_ICt_Maps_AppIcon_79x79_b6b7ac, {
        { IW_V00_RESOURCE_ABI, 79, 79, 158, 79, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 7264, 0xe92ad959u },
        v00_ICt_Maps_AppIcon_79x79_b6b7ac_map
    } },
    { "ICt_Weather_AppIcon-79x79-f3e41a", &v00_ICt_Weather_AppIcon_79x79_f3e41a, {
        { IW_V00_RESOURCE_ABI, 79, 79, 158, 79, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 6711, 0x8d620f59u },
        v00_ICt_Weather_AppIcon_79x79_f3e41a_map
    } },
    { "ICt_Alarms_AppIcon-67x67-f2d14f", &v00_ICt_Alarms_AppIcon_67x67_f2d14f, {
        { IW_V00_RESOURCE_ABI, 67, 67, 134, 67, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 5640, 0x7b51af61u },
        v00_ICt_Alarms_AppIcon_67x67_f2d14f_map
    } },
    { "ICt_Compass_AppIcon-64x64-13980a", &v00_ICt_Compass_AppIcon_64x64_13980a, {
        { IW_V00_RESOURCE_ABI, 64, 64, 128, 64, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 4896, 0x1a677cbeu },
        v00_ICt_Compass_AppIcon_64x64_13980a_map
    } },
    { "ICt_Photos_AppIcon-78x78-257cb6", &v00_ICt_Photos_AppIcon_78x78_257cb6, {
        { IW_V00_RESOURCE_ABI, 78, 78, 156, 78, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 6728, 0x8632ae84u },
        v00_ICt_Photos_AppIcon_78x78_257cb6_map
    } },
    { "ICt_Sleep_AppIcon-64x64-944c3e", &v00_ICt_Sleep_AppIcon_64x64_944c3e, {
        { IW_V00_RESOURCE_ABI, 64, 64, 128, 64, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 4215, 0xb44b2858u },
        v00_ICt_Sleep_AppIcon_64x64_944c3e_map
    } },
    { "music-59x59-4d73be", &v00_music_59x59_4d73be, {
        { IW_V00_RESOURCE_ABI, 59, 59, 118, 59, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 1460, 0xb818e3f1u },
        v00_music_59x59_4d73be_map
    } },
    { "restore-25x25-e7a179", &v00_restore_25x25_e7a179, {
        { IW_V00_RESOURCE_ABI, 25, 25, 50, 25, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 568, 0xfe27fd6fu },
        v00_restore_25x25_e7a179_map
    } },
    { "decoration-92x92-f49b0e", &v00_decoration_92x92_f49b0e, {
        { IW_V00_RESOURCE_ABI, 92, 92, 184, 92, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 5427, 0x249e4879u },
        v00_decoration_92x92_f49b0e_map
    } },
    { "flower-65x65-e07948", &v00_flower_65x65_e07948, {
        { IW_V00_RESOURCE_ABI, 65, 65, 130, 65, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 2247, 0x63cb641au },
        v00_flower_65x65_e07948_map
    } },
    { "run-61x61-9c588c", &v00_run_61x61_9c588c, {
        { IW_V00_RESOURCE_ABI, 61, 61, 122, 61, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 1505, 0xc83ec7afu },
        v00_run_61x61_9c588c_map
    } },
    { "close-26x26-6150ef", &v00_close_26x26_6150ef, {
        { IW_V00_RESOURCE_ABI, 26, 26, 52, 26, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 381, 0xe2ba9a06u },
        v00_close_26x26_6150ef_map
    } },
    { "back-26x26-e153fd", &v00_back_26x26_e153fd, {
        { IW_V00_RESOURCE_ABI, 26, 26, 52, 26, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 357, 0x42de26d8u },
        v00_back_26x26_e153fd_map
    } },
    { "decoration-333x333-f15119", &v00_decoration_333x333_f15119, {
        { IW_V00_RESOURCE_ABI, 333, 333, 666, 333, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 16087, 0x44c0f59eu },
        v00_decoration_333x333_f15119_map
    } },
    { "close-32x32-f30976", &v00_close_32x32_f30976, {
        { IW_V00_RESOURCE_ABI, 32, 32, 64, 32, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 420, 0xedd8f195u },
        v00_close_32x32_f30976_map
    } },
    { "check-36x36-fb2c9e", &v00_check_36x36_fb2c9e, {
        { IW_V00_RESOURCE_ABI, 36, 36, 72, 36, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 567, 0x24c43f69u },
        v00_check_36x36_fb2c9e_map
    } },
    { "sun-28x28-40e472", &v00_sun_28x28_40e472, {
        { IW_V00_RESOURCE_ABI, 28, 28, 56, 28, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 718, 0xabe8bef1u },
        v00_sun_28x28_40e472_map
    } },
    { "sun-43x43-addcf2", &v00_sun_43x43_addcf2, {
        { IW_V00_RESOURCE_ABI, 43, 43, 86, 43, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 1188, 0x44401ef8u },
        v00_sun_43x43_addcf2_map
    } },
    { "ICt_connectediPhone-24x24-acc9cd", &v00_ICt_connectediPhone_24x24_acc9cd, {
        { IW_V00_RESOURCE_ABI, 24, 24, 48, 24, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 466, 0x25bcef41u },
        v00_ICt_connectediPhone_24x24_acc9cd_map
    } },
    { "ICt_silentMode-24x24-cdf64a", &v00_ICt_silentMode_24x24_cdf64a, {
        { IW_V00_RESOURCE_ABI, 24, 24, 48, 24, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 785, 0x43b3e25eu },
        v00_ICt_silentMode_24x24_cdf64a_map
    } },
    { "ICt_statusDNDFocus-24x24-20a117", &v00_ICt_statusDNDFocus_24x24_20a117, {
        { IW_V00_RESOURCE_ABI, 24, 24, 48, 24, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 615, 0x2a5debacu },
        v00_ICt_statusDNDFocus_24x24_20a117_map
    } },
    { "ICt_Wifi-58x58-0ecb11", &v00_ICt_Wifi_58x58_0ecb11, {
        { IW_V00_RESOURCE_ABI, 58, 58, 116, 58, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 1863, 0xe99f4806u },
        v00_ICt_Wifi_58x58_0ecb11_map
    } },
    { "ICt_airplaneMode-58x58-99f564", &v00_ICt_airplaneMode_58x58_99f564, {
        { IW_V00_RESOURCE_ABI, 58, 58, 116, 58, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 1882, 0x358d8c42u },
        v00_ICt_airplaneMode_58x58_99f564_map
    } },
    { "ICt_pingPhone-58x58-78008a", &v00_ICt_pingPhone_58x58_78008a, {
        { IW_V00_RESOURCE_ABI, 58, 58, 116, 58, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 2631, 0x0859764eu },
        v00_ICt_pingPhone_58x58_78008a_map
    } },
    { "ICt_flashlight-58x58-18dfd7", &v00_ICt_flashlight_58x58_18dfd7, {
        { IW_V00_RESOURCE_ABI, 58, 58, 116, 58, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 1472, 0x77291563u },
        v00_ICt_flashlight_58x58_18dfd7_map
    } },
    { "ICt_doNotDisturb-58x58-e14f6c", &v00_ICt_doNotDisturb_58x58_e14f6c, {
        { IW_V00_RESOURCE_ABI, 58, 58, 116, 58, IW_V00_RESOURCE_EZIP_RGB565A8, 0x1cu, 0x18u, false, 1461, 0x46a1d4b7u },
        v00_ICt_doNotDisturb_58x58_e14f6c_map
    } },
};

size_t iw_v00_resource_catalog_count(void)
{
    return sizeof(catalog) / sizeof(catalog[0]);
}

iw_v00_resource_result_t iw_v00_resource_catalog_validate(size_t index, bool for_release)
{
    if (index >= iw_v00_resource_catalog_count())
        return IW_V00_RESOURCE_INVALID;

    const catalog_entry_t *entry = &catalog[index];
    const lv_image_dsc_t *image = entry->image;
    const iw_v00_resource_image_t view = {
        image->header.w, image->header.h, image->header.stride,
        image->header.cf, image->data_size, image->data
    };
    if (image->header.magic != LV_IMAGE_HEADER_MAGIC ||
        image->header.flags != LV_IMAGE_FLAGS_EZIP)
        return IW_V00_RESOURCE_BINDING;
    return iw_v00_resource_validate_binding(&entry->binding, &view, for_release);
}

const lv_image_dsc_t *iw_v00_resource_catalog_image(size_t index)
{
    return index < iw_v00_resource_catalog_count() ? catalog[index].image : NULL;
}
