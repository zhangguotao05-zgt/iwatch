#include <math.h>
#include "cell_transform.h"

/* x64 主机测试替身；目标固件及 x86 SDK 专项测试均链接 SiFli 原始库。 */
int get_icon_transform_param(float x, float y, float icon_r,
                             float *out_x, float *out_y, float *out_r,
                             float *distance, float screen_w, float screen_h)
{
    if (!out_x || !out_y || !out_r || !distance || icon_r <= 0.0f) return 0;
    float dx = x - screen_w / 2.0f, dy = y - screen_h / 2.0f;
    *distance = sqrtf(dx * dx + dy * dy);
    float ratio = *distance / (screen_h / 2.0f);
    if (ratio > 1.4f) return 0;
    *out_x = x + dx * (0.08f * (1.0f - ratio));
    *out_y = y + dy * (0.08f * (1.0f - ratio));
    *out_r = icon_r * (1.16f - 0.40f * ratio);
    return 1;
}
