#include "iw_time_rtc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bf0_hal.h"
#include "rtdevice.h"

#define SESSION_INVERSE_REGISTER 28u
#define SESSION_REGISTER 31u

static uint32_t backup_registers[32];
static uint32_t rtc_seconds;
static uint32_t random_value;
static bool rtc_present;
static bool rng_init_success;
static bool rng_generate_success;
static unsigned rng_init_count;
static unsigned rng_deinit_count;
static int rtc_instance;

void *hwp_trng = (void *)(uintptr_t)0x1234u;

uint32_t HAL_Get_backup(uint8_t index)
{
    assert(index < 32u);
    return backup_registers[index];
}

void HAL_Set_backup(uint8_t index, uint32_t value)
{
    assert(index < 32u);
    backup_registers[index] = value;
}

HAL_StatusTypeDef HAL_RNG_Init(RNG_HandleTypeDef *rng)
{
    assert(rng && rng->Instance == hwp_trng);
    rng_init_count++;
    return rng_init_success ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HAL_RNG_DeInit(RNG_HandleTypeDef *rng)
{
    assert(rng && rng->Instance == hwp_trng);
    rng_deinit_count++;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_RNG_GenerateRandomSeed(RNG_HandleTypeDef *rng, uint32_t *value)
{
    assert(rng && value);
    *value = UINT32_C(0x13579bdf);
    return rng_generate_success ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HAL_RNG_GenerateRandomNumber(RNG_HandleTypeDef *rng, uint32_t *value)
{
    assert(rng && value);
    *value = random_value;
    return rng_generate_success ? HAL_OK : HAL_ERROR;
}

rt_device_t rt_device_find(const char *name)
{
    assert(strcmp(name, "rtc") == 0);
    return rtc_present ? &rtc_instance : RT_NULL;
}

rt_err_t rt_device_control(rt_device_t device, int command, void *argument)
{
    assert(device == &rtc_instance && argument);
    if (command == RT_DEVICE_CTRL_RTC_GET_TIME)
        *(uint32_t *)argument = rtc_seconds;
    else if (command == RT_DEVICE_CTRL_RTC_SET_TIME)
        rtc_seconds = *(const uint32_t *)argument;
    else
        return -1;
    return RT_EOK;
}

static void reset_mocks(void)
{
    memset(backup_registers, 0, sizeof(backup_registers));
    rtc_seconds = 0u;
    random_value = 0u;
    rtc_present = true;
    rng_init_success = true;
    rng_generate_success = true;
    rng_init_count = 0u;
    rng_deinit_count = 0u;
}

static void test_rtc_trust_marker(void)
{
    uint32_t seconds = 0u;
    int16_t offset = 0;

    reset_mocks();
    assert(iw_time_rtc_available());
    assert(!iw_time_rtc_read(&seconds, &offset));
    assert(iw_time_rtc_write(1789529179u, 480));
    assert(iw_time_rtc_read(&seconds, &offset));
    assert(seconds == 1789529179u && offset == 480);
    assert(!iw_time_rtc_write(seconds, 841));
    rtc_present = false;
    assert(!iw_time_rtc_available());
    assert(!iw_time_rtc_read(&seconds, &offset));
}

static void test_session_cold_and_soft_boot(void)
{
    uint32_t session = 0u;

    reset_mocks();
    random_value = UINT32_C(0xf1234567);
    assert(iw_time_rtc_next_session(&session));
    assert(session == UINT32_C(0x71234567));
    assert(backup_registers[SESSION_REGISTER] == session);
    assert(backup_registers[SESSION_INVERSE_REGISTER] == ~session);
    assert(rng_init_count == 1u && rng_deinit_count == 1u);

    assert(iw_time_rtc_next_session(&session));
    assert(session == UINT32_C(0x71234568));
    assert(rng_init_count == 1u && rng_deinit_count == 1u);

    /* 备份域损坏时不能继续旧序列，零熵也必须映射为合法 session。 */
    backup_registers[SESSION_INVERSE_REGISTER] = 0u;
    random_value = 0u;
    assert(iw_time_rtc_next_session(&session));
    assert(session == 1u);
    assert(rng_init_count == 2u && rng_deinit_count == 2u);
}

static void test_session_failure_and_exhaustion(void)
{
    uint32_t session = 99u;

    reset_mocks();
    rng_init_success = false;
    assert(!iw_time_rtc_next_session(&session));
    assert(session == 99u);
    assert(backup_registers[SESSION_REGISTER] == 0u);

    reset_mocks();
    backup_registers[SESSION_REGISTER] = UINT32_MAX;
    backup_registers[SESSION_INVERSE_REGISTER] = 0u;
    assert(!iw_time_rtc_next_session(&session));
    assert(rng_init_count == 0u);

    reset_mocks();
    rng_generate_success = false;
    assert(!iw_time_rtc_next_session(&session));
    assert(rng_init_count == 1u && rng_deinit_count == 1u);
}

int main(void)
{
    test_rtc_trust_marker();
    test_session_cold_and_soft_boot();
    test_session_failure_and_exhaustion();
    puts("RTC trust/session tests passed");
    return 0;
}
