#ifndef IW_KEYS_H
#define IW_KEYS_H

#include <stdbool.h>
#include <stdint.h>

enum { IW_KEY_CROWN, IW_KEY_SIDE, IW_KEY_COUNT, IW_KEY_OUTPUTS = 3, IW_ROTATION_LIMIT = 32 };
typedef enum { IW_KEY_NONE, IW_KEY_SINGLE, IW_KEY_DOUBLE, IW_KEY_HOLD, IW_KEY_UNLOCK } iw_key_kind_t;
typedef struct { unsigned key; iw_key_kind_t kind; } iw_key_signal_t;
typedef struct {
    uint32_t double_ticks, hold_ticks[IW_KEY_COUNT], unlock_ticks, release_ticks;
} iw_keys_config_t;
typedef struct {
    uint32_t pressed_at, released_at, stable_at;
    bool down, second, pending, held, wait_release, stable;
} iw_key_state_t;
typedef struct {
    iw_keys_config_t config;
    iw_key_state_t keys[IW_KEY_COUNT];
    uint32_t last_tick;
    int32_t rotation;
    bool clock_valid, water_lock;
} iw_keys_t;

/* 纯 C、有界、无堆分配；时间单位由调用方统一，间隔须小于半个 uint32_t 周期。 */
bool iw_keys_init(iw_keys_t *keys, const iw_keys_config_t *config);
void iw_keys_cancel(iw_keys_t *keys);
void iw_keys_water_lock(iw_keys_t *keys, bool locked);
/* out 至少 IW_KEY_OUTPUTS 项；先结算截止事件，再消费边沿。重复边沿不产生新手势。 */
unsigned iw_keys_edge(iw_keys_t *keys, unsigned key, bool pressed, uint32_t now, iw_key_signal_t *out);
unsigned iw_keys_advance(iw_keys_t *keys, uint32_t now, iw_key_signal_t *out);
uint32_t iw_keys_wait(const iw_keys_t *keys, uint32_t now);
/* 仅当原始队列已排空时采样；GPIO 由调用方在临界区外读取。 */
void iw_keys_recover(iw_keys_t *keys, unsigned key, bool pressed, uint32_t now);
void iw_keys_rotate(iw_keys_t *keys, int32_t delta, bool touch_down);
int32_t iw_keys_take_rotation(iw_keys_t *keys, bool touch_down);

typedef enum { IW_INPUT_NORMAL, IW_INPUT_EDIT, IW_INPUT_OVERLAY, IW_INPUT_ALERT,
               IW_INPUT_LOCKED, IW_INPUT_WATER, IW_INPUT_RECOVERY } iw_input_context_t;
typedef enum { IW_INTENT_NONE, IW_INTENT_HOME, IW_INTENT_DISMISS, IW_INTENT_CONTROL,
               IW_INTENT_SWITCHER, IW_INTENT_WALLET, IW_INTENT_VOICE, IW_INTENT_POWER,
               IW_INTENT_UNLOCK, IW_INTENT_RECOVER } iw_input_intent_t;
/* 上下文由 GUI 所有者按优先级提供；未实现的目标由路由层明确拒绝。 */
iw_input_intent_t iw_keys_intent(iw_key_signal_t signal, iw_input_context_t context);

#endif
