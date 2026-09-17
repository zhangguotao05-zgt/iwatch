#ifndef TEST_KEY_BUTTON_H
#define TEST_KEY_BUTTON_H
#include <stdint.h>
#include <stdbool.h>
#define BSP_KEY1_PIN 150
#define BSP_KEY2_PIN 152
#define BSP_KEY1_ACTIVE_HIGH
#define BSP_KEY2_ACTIVE_HIGH
#define SF_EOK 0
#define PIN_MODE_INPUT 0
typedef enum { BUTTON_ACTIVE_LOW, BUTTON_ACTIVE_HIGH } button_active_state_t;
typedef enum { BUTTON_PRESSED, BUTTON_RELEASED, BUTTON_CLICKED, BUTTON_LONG_PRESSED } button_action_t;
typedef struct { int32_t pin; button_active_state_t active_state; unsigned mode, debounce_time;
    void (*button_handler)(int32_t, button_action_t); } button_cfg_t;
int32_t button_init(const button_cfg_t *cfg);
int button_enable(int32_t id);
int button_disable(int32_t id);
bool button_is_pressed(int32_t id);
#endif
