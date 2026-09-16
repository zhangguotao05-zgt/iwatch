#include "iw_time_rtc.h"

#include <bf0_hal.h>
#include <rtdevice.h>

#define IW_RTC_DEVICE_NAME "rtc"
#define IW_RTC_SESSION_INVERSE_REGISTER 28u
#define IW_RTC_STATE_INVERSE_REGISTER 29u
#define IW_RTC_STATE_REGISTER 30u
#define IW_RTC_SESSION_REGISTER 31u
#define IW_RTC_STATE_SIGNATURE UINT32_C(0x49570000)
#define IW_RTC_STATE_SIGNATURE_MASK UINT32_C(0xffff0000)
#define IW_RTC_COLD_SESSION_MASK UINT32_C(0x7fffffff)

static rt_device_t rtc_device(void)
{
    return rt_device_find(IW_RTC_DEVICE_NAME);
}

bool iw_time_rtc_available(void)
{
    return rtc_device() != RT_NULL;
}

bool iw_time_rtc_read(uint32_t *utc_seconds, int16_t *offset_minutes)
{
    rt_device_t device;
    uint32_t state;

    if (!utc_seconds || !offset_minutes) return false;
    device = rtc_device();
    if (!device) return false;
    state = HAL_Get_backup(IW_RTC_STATE_REGISTER);
    if (HAL_Get_backup(IW_RTC_STATE_INVERSE_REGISTER) != ~state) return false;
    if ((state & IW_RTC_STATE_SIGNATURE_MASK) != IW_RTC_STATE_SIGNATURE) return false;
    *offset_minutes = (int16_t)(state & UINT16_MAX);
    if (*offset_minutes < -840 || *offset_minutes > 840) return false;
    return rt_device_control(device, RT_DEVICE_CTRL_RTC_GET_TIME, utc_seconds) == RT_EOK;
}

bool iw_time_rtc_write(uint32_t utc_seconds, int16_t offset_minutes)
{
    rt_device_t device = rtc_device();
    uint32_t state;

    if (!device || offset_minutes < -840 || offset_minutes > 840) return false;

    /* 先撤销可信标记，避免 RTC 只完成一半写入后仍被当作有效时间。 */
    HAL_Set_backup(IW_RTC_STATE_REGISTER, 0u);
    HAL_Set_backup(IW_RTC_STATE_INVERSE_REGISTER, 0u);
    if (HAL_Get_backup(IW_RTC_STATE_REGISTER) != 0u ||
            HAL_Get_backup(IW_RTC_STATE_INVERSE_REGISTER) != 0u)
        return false;
    if (rt_device_control(device, RT_DEVICE_CTRL_RTC_SET_TIME, &utc_seconds) != RT_EOK)
        return false;

    state = IW_RTC_STATE_SIGNATURE | (uint16_t)offset_minutes;
    HAL_Set_backup(IW_RTC_STATE_REGISTER, state);
    HAL_Set_backup(IW_RTC_STATE_INVERSE_REGISTER, ~state);
    return HAL_Get_backup(IW_RTC_STATE_REGISTER) == state &&
           HAL_Get_backup(IW_RTC_STATE_INVERSE_REGISTER) == ~state;
}

static bool session_entropy(uint32_t *entropy)
{
    RNG_HandleTypeDef rng = {0};
    uint32_t seed;
    bool success;

    if (!entropy) return false;
    rng.Instance = hwp_trng;
    if (HAL_RNG_Init(&rng) != HAL_OK) return false;
    success = HAL_RNG_GenerateRandomSeed(&rng, &seed) == HAL_OK &&
              HAL_RNG_GenerateRandomNumber(&rng, entropy) == HAL_OK;
    (void)HAL_RNG_DeInit(&rng);
    return success;
}

static bool session_store(uint32_t session_id)
{
    /* 反码最后写入；任意一次中断都只会留下无效记录。 */
    HAL_Set_backup(IW_RTC_SESSION_INVERSE_REGISTER, 0u);
    if (HAL_Get_backup(IW_RTC_SESSION_INVERSE_REGISTER) != 0u) return false;
    HAL_Set_backup(IW_RTC_SESSION_REGISTER, session_id);
    HAL_Set_backup(IW_RTC_SESSION_INVERSE_REGISTER, ~session_id);
    return HAL_Get_backup(IW_RTC_SESSION_REGISTER) == session_id &&
           HAL_Get_backup(IW_RTC_SESSION_INVERSE_REGISTER) == ~session_id;
}

bool iw_time_rtc_next_session(uint32_t *session_id)
{
    uint32_t previous;
    uint32_t inverse;
    uint32_t next;

    if (!session_id) return false;
    previous = HAL_Get_backup(IW_RTC_SESSION_REGISTER);
    inverse = HAL_Get_backup(IW_RTC_SESSION_INVERSE_REGISTER);
    if (previous != 0u && inverse == ~previous)
    {
        if (previous == UINT32_MAX) return false;
        next = previous + 1u;
    }
    else
    {
        /* 备份域丢失后用 TRNG 降低旧 session 碰撞概率；该值不提供唯一性或安全保证。 */
        if (!session_entropy(&next)) return false;
        next &= IW_RTC_COLD_SESSION_MASK;
        if (next == 0u) next = 1u;
    }
    if (!session_store(next)) return false;
    *session_id = next;
    return true;
}
