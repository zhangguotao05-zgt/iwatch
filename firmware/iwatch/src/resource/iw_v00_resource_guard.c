#include "iw_v00_resource_guard.h"

enum { MAX_WIDTH = 390, MAX_HEIGHT = 450, MAX_PAYLOAD = 1048576 };

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8u) | data[1]);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) | data[3];
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

iw_v00_resource_result_t iw_v00_resource_validate(const iw_v00_resource_meta_t *meta,
                                                  const uint8_t *payload, size_t length,
                                                  bool for_release)
{
    if (!meta || !payload) return IW_V00_RESOURCE_INVALID;
    if (meta->version != IW_V00_RESOURCE_ABI) return IW_V00_RESOURCE_VERSION;
    if (!meta->width || meta->width > MAX_WIDTH ||
        !meta->height || meta->height > MAX_HEIGHT)
        return IW_V00_RESOURCE_GEOMETRY;
    if (meta->format < IW_V00_RESOURCE_RAW_RGB565 ||
        meta->format > IW_V00_RESOURCE_EZIP_RGB565A8 ||
        meta->rgb_stride_bytes != (uint32_t)meta->width * 2u ||
        meta->alpha_stride_bytes != ((meta->format & 1u) ? 0u : meta->width))
        return IW_V00_RESOURCE_GEOMETRY;
    if (!meta->payload_bytes || meta->payload_bytes > MAX_PAYLOAD ||
        length != meta->payload_bytes)
        return IW_V00_RESOURCE_SIZE;
    if (meta->format <= IW_V00_RESOURCE_RAW_RGB565A8 &&
        length != (size_t)meta->width * meta->height *
                      ((meta->format == IW_V00_RESOURCE_RAW_RGB565) ? 2u : 3u))
        return IW_V00_RESOURCE_SIZE;
    if (meta->format >= IW_V00_RESOURCE_EZIP_RGB565) {
        /* 以下偏移固定于本工程锁定的 eZIP 2.6.4、-dpt 1 转换结果。 */
        if (length < 16u || read_be32(payload) != length)
            return IW_V00_RESOURCE_SIZE;
        const bool rgb565 = meta->format == IW_V00_RESOURCE_EZIP_RGB565;
        const bool codec_ok = rgb565 ?
            (meta->ezip_codec == 0x18u && meta->ezip_flags == 0x10u) :
            (meta->ezip_codec == 0x1cu && meta->ezip_flags == 0x18u);
        if (!codec_ok || payload[4] != meta->ezip_codec ||
            payload[5] != meta->ezip_flags ||
            read_be16(payload + 8u) != meta->width ||
            read_be16(payload + 10u) != meta->height)
            return IW_V00_RESOURCE_GEOMETRY;
    }
    if (for_release && !meta->release_allowed)
        return IW_V00_RESOURCE_NOT_PUBLISHABLE;
    if (crc32_bytes(payload, length) != meta->crc32)
        return IW_V00_RESOURCE_INTEGRITY;
    return IW_V00_RESOURCE_OK;
}

iw_v00_resource_result_t iw_v00_resource_validate_binding(
    const iw_v00_resource_binding_t *binding,
    const iw_v00_resource_image_t *image, bool for_release)
{
    if (!binding || !image || !binding->payload || !image->data)
        return IW_V00_RESOURCE_INVALID;

    const iw_v00_resource_meta_t *meta = &binding->meta;
    if (meta->format != IW_V00_RESOURCE_EZIP_RGB565 &&
        meta->format != IW_V00_RESOURCE_EZIP_RGB565A8)
        return IW_V00_RESOURCE_BINDING;
    if (image->data != binding->payload || image->width != meta->width ||
        image->height != meta->height || image->stride_bytes != 0u ||
        image->color_format != ((meta->format == IW_V00_RESOURCE_EZIP_RGB565) ? 1u : 2u) ||
        image->data_size != meta->payload_bytes)
        return IW_V00_RESOURCE_BINDING;

    return iw_v00_resource_validate(meta, binding->payload,
                                    image->data_size, for_release);
}
