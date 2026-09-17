#include "iw_keys.h"
#include <limits.h>
#include <string.h>

static bool valid_interval(uint32_t value) { return value && value <= INT32_MAX; }

bool iw_keys_init(iw_keys_t *keys, const iw_keys_config_t *config)
{
    if (!keys || !config || !valid_interval(config->double_ticks) ||
        !valid_interval(config->unlock_ticks) || !valid_interval(config->release_ticks)) return false;
    for (unsigned i = 0; i < IW_KEY_COUNT; i++)
        if (!valid_interval(config->hold_ticks[i])) return false;
    memset(keys, 0, sizeof(*keys));
    keys->config = *config;
    return true;
}

void iw_keys_cancel(iw_keys_t *keys)
{
    if (!keys) return;
    memset(keys->keys, 0, sizeof(keys->keys));
    for (unsigned i = 0; i < IW_KEY_COUNT; i++) keys->keys[i].wait_release = true;
    keys->rotation = 0;
}

void iw_keys_water_lock(iw_keys_t *keys, bool locked)
{
    if (keys && keys->water_lock != locked) {
        iw_keys_cancel(keys);
        keys->water_lock = locked;
    }
}

static bool accept_time(iw_keys_t *keys, uint32_t now)
{
    if (keys->clock_valid && now - keys->last_tick > INT32_MAX) {
        /* 过期边沿不能重新激活已取消的组合；等待新的可信释放。 */
        iw_keys_cancel(keys);
        return false;
    }
    keys->last_tick = now;
    keys->clock_valid = true;
    return true;
}

static uint32_t hold_ticks(const iw_keys_t *keys, unsigned key)
{
    return keys->water_lock && key == IW_KEY_CROWN ? keys->config.unlock_ticks : keys->config.hold_ticks[key];
}

unsigned iw_keys_advance(iw_keys_t *keys, uint32_t now, iw_key_signal_t *out)
{
    unsigned count = 0;
    if (!keys || !out || !accept_time(keys, now)) return 0;
    for (unsigned i = 0; i < IW_KEY_COUNT; i++) {
        iw_key_state_t *state = &keys->keys[i];
        if (state->wait_release) continue;
        if (state->down && !state->held && now - state->pressed_at >= hold_ticks(keys, i)) {
            state->held = true;
            state->pending = state->second = false;
            out[count++] = (iw_key_signal_t){i, keys->water_lock && i == IW_KEY_CROWN ? IW_KEY_UNLOCK : IW_KEY_HOLD};
        } else if (state->pending && now - state->released_at >= keys->config.double_ticks) {
            state->pending = false;
            out[count++] = (iw_key_signal_t){i, IW_KEY_SINGLE};
        }
    }
    return count;
}

unsigned iw_keys_edge(iw_keys_t *keys, unsigned key, bool pressed, uint32_t now, iw_key_signal_t *out)
{
    if (!keys || !out || key >= IW_KEY_COUNT) return 0;
    if (keys->clock_valid && now - keys->last_tick > INT32_MAX) {
        iw_keys_cancel(keys);
        return 0;
    }
    unsigned count = iw_keys_advance(keys, now, out);
    iw_key_state_t *state = &keys->keys[key];
    if (state->wait_release) {
        if (!pressed) memset(state, 0, sizeof(*state));
        return count;
    }
    if (pressed == state->down) return count;
    state->down = pressed;
    if (pressed) {
        state->second = state->pending;
        state->pending = state->held = false;
        state->pressed_at = now;
    } else if (!state->held) {
        if (state->second) {
            state->second = false;
            out[count++] = (iw_key_signal_t){key, IW_KEY_DOUBLE};
        } else {
            state->pending = true;
            state->released_at = now;
        }
    }
    return count;
}

uint32_t iw_keys_wait(const iw_keys_t *keys, uint32_t now)
{
    uint32_t wait = UINT32_MAX;
    if (!keys) return wait;
    for (unsigned i = 0; i < IW_KEY_COUNT; i++) {
        const iw_key_state_t *state = &keys->keys[i];
        if (state->wait_release) {
            if (keys->config.release_ticks < wait) wait = keys->config.release_ticks;
            continue;
        }
        uint32_t period, start;
        if (state->down && !state->held) { period = hold_ticks(keys, i); start = state->pressed_at; }
        else if (state->pending) { period = keys->config.double_ticks; start = state->released_at; }
        else continue;
        uint32_t elapsed = now - start;
        uint32_t remaining = elapsed >= period ? 0 : period - elapsed;
        if (remaining < wait) wait = remaining;
    }
    return wait;
}

void iw_keys_recover(iw_keys_t *keys, unsigned key, bool pressed, uint32_t now)
{
    if (!keys || key >= IW_KEY_COUNT) return;
    iw_key_state_t *state = &keys->keys[key];
    if (!state->wait_release) return;
    if (pressed) { state->stable = false; return; }
    if (!state->stable) { state->stable = true; state->stable_at = now; }
    else if (now - state->stable_at <= INT32_MAX && now - state->stable_at >= keys->config.release_ticks)
        memset(state, 0, sizeof(*state));
}

void iw_keys_rotate(iw_keys_t *keys, int32_t delta, bool touch_down)
{
    if (!keys) return;
    if (touch_down) { keys->rotation = 0; return; }
    int64_t sum = (int64_t)keys->rotation + delta;
    keys->rotation = sum > IW_ROTATION_LIMIT ? IW_ROTATION_LIMIT :
                     sum < -IW_ROTATION_LIMIT ? -IW_ROTATION_LIMIT : (int32_t)sum;
}

int32_t iw_keys_take_rotation(iw_keys_t *keys, bool touch_down)
{
    if (!keys) return 0;
    int32_t delta = touch_down ? 0 : keys->rotation;
    keys->rotation = 0;
    return delta;
}

iw_input_intent_t iw_keys_intent(iw_key_signal_t signal, iw_input_context_t context)
{
    if (signal.key >= IW_KEY_COUNT || signal.kind == IW_KEY_NONE ||
        (unsigned)context > IW_INPUT_RECOVERY) return IW_INTENT_NONE;
    if (context == IW_INPUT_RECOVERY)
        return signal.key == IW_KEY_CROWN && signal.kind == IW_KEY_SINGLE ? IW_INTENT_RECOVER : IW_INTENT_NONE;
    if (context == IW_INPUT_WATER)
        return signal.key == IW_KEY_CROWN && signal.kind == IW_KEY_UNLOCK ? IW_INTENT_UNLOCK : IW_INTENT_NONE;
    if (context == IW_INPUT_LOCKED) return IW_INTENT_NONE;
    if (context == IW_INPUT_ALERT || context == IW_INPUT_OVERLAY)
        return signal.kind == IW_KEY_SINGLE ? IW_INTENT_DISMISS : IW_INTENT_NONE;
    if (signal.kind == IW_KEY_SINGLE) return signal.key == IW_KEY_CROWN ? IW_INTENT_HOME : IW_INTENT_CONTROL;
    if (signal.kind == IW_KEY_DOUBLE) return signal.key == IW_KEY_CROWN ? IW_INTENT_SWITCHER : IW_INTENT_WALLET;
    if (signal.kind == IW_KEY_HOLD) return signal.key == IW_KEY_CROWN ? IW_INTENT_VOICE : IW_INTENT_POWER;
    return IW_INTENT_NONE;
}
