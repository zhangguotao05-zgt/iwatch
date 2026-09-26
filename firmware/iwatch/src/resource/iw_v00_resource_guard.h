#ifndef IW_V00_RESOURCE_GUARD_H
#define IW_V00_RESOURCE_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IW_V00_RESOURCE_ABI 2u

typedef enum {
    IW_V00_RESOURCE_RAW_RGB565 = 1,
    IW_V00_RESOURCE_RAW_RGB565A8 = 2,
    IW_V00_RESOURCE_EZIP_RGB565 = 3,
    IW_V00_RESOURCE_EZIP_RGB565A8 = 4
} iw_v00_resource_format_t;

typedef enum {
    IW_V00_RESOURCE_OK = 0,
    IW_V00_RESOURCE_INVALID,
    IW_V00_RESOURCE_VERSION,
    IW_V00_RESOURCE_GEOMETRY,
    IW_V00_RESOURCE_SIZE,
    IW_V00_RESOURCE_INTEGRITY,
    IW_V00_RESOURCE_NOT_PUBLISHABLE,
    IW_V00_RESOURCE_BINDING
} iw_v00_resource_result_t;

typedef struct {
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t rgb_stride_bytes;
    uint16_t alpha_stride_bytes;
    uint8_t format;
    uint8_t ezip_codec;
    uint8_t ezip_flags;
    bool release_allowed;
    uint32_t payload_bytes;
    uint32_t crc32;
} iw_v00_resource_meta_t;

typedef struct {
    iw_v00_resource_meta_t meta;
    const uint8_t *payload;
} iw_v00_resource_binding_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t stride_bytes;
    uint8_t color_format;
    uint32_t data_size;
    const uint8_t *data;
} iw_v00_resource_image_t;

/* 只读、无分配；校验固定元数据、字节完整性和锁定 eZIP 格式的长度/尺寸标头。
 * 不解析压缩像素；调用方仍须使用固定生成表，并处理 SDK 解码失败。 */
iw_v00_resource_result_t iw_v00_resource_validate(const iw_v00_resource_meta_t *meta,
                                                  const uint8_t *payload, size_t length,
                                                  bool for_release);

/* binding 必须来自生成的只读目录；将图像描述符和固定载荷配对后才允许交给解码器。
 * 只在注册时调用一次，不能替代 SDK 解码失败、忙状态及内存不足处理。 */
iw_v00_resource_result_t iw_v00_resource_validate_binding(
    const iw_v00_resource_binding_t *binding,
    const iw_v00_resource_image_t *image, bool for_release);

#endif
