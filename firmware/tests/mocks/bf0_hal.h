#ifndef IW_TEST_BF0_HAL_H
#define IW_TEST_BF0_HAL_H

#include <stdint.h>

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_TIMEOUT = 2
} HAL_StatusTypeDef;

typedef struct
{
    void *Instance;
} RNG_HandleTypeDef;

extern void *hwp_trng;

uint32_t HAL_Get_backup(uint8_t index);
void HAL_Set_backup(uint8_t index, uint32_t value);
HAL_StatusTypeDef HAL_RNG_Init(RNG_HandleTypeDef *rng);
HAL_StatusTypeDef HAL_RNG_DeInit(RNG_HandleTypeDef *rng);
HAL_StatusTypeDef HAL_RNG_GenerateRandomSeed(RNG_HandleTypeDef *rng, uint32_t *value);
HAL_StatusTypeDef HAL_RNG_GenerateRandomNumber(RNG_HandleTypeDef *rng, uint32_t *value);

#endif
