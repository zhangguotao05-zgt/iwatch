#include "iw_v00_typography.h"

#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
static const iw_v00_type_style_t styles[IW_V00_TYPE_COUNT] = {
    {30, 500, 0}, {30, 600, 0}, {81, 400, -4},
    {28, 500, 0}, {26, 500, 0}, {25, 500, 0},
    {26, 500, 0}, {25, 600, 0}, {49, 600, 0},
    {25, 400, 0}, {26, 500, 0}, {22, 400, 0},
    {44, 400, 0}, {35, 300, 0}, {23, 400, 0},
    {26, 400, 0}, {20, 400, 0}, {31, 500, 0}
};

const iw_v00_type_style_t *iw_v00_type_style(iw_v00_type_id_t id)
{
    return (unsigned)id < IW_V00_TYPE_COUNT ? &styles[id] : 0;
}
#endif
