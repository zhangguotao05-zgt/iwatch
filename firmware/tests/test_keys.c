#include "iw_keys.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static const iw_keys_config_t config = {280, {800, 1500}, 2000, 20};
static iw_keys_t keys;
static iw_key_signal_t output[IW_KEY_OUTPUTS];
static void reset(void) { assert(iw_keys_init(&keys, &config)); }
static unsigned edge(unsigned key, bool pressed, uint32_t tick)
{
    return iw_keys_edge(&keys, key, pressed, tick, output);
}

static void timing(uint32_t start)
{
    reset();
    assert(!edge(0, true, start));
    assert(!edge(0, true, start + 1));
    assert(!edge(0, false, start + 20));
    assert(!edge(0, false, start + 21));
    assert(iw_keys_wait(&keys, start + 21) == 279);
    assert(!iw_keys_advance(&keys, start + 299, output));
    assert(iw_keys_advance(&keys, start + 300, output) == 1);
    assert(output[0].key == 0 && output[0].kind == IW_KEY_SINGLE);
    assert(!iw_keys_advance(&keys, start + 301, output));
    assert(iw_keys_wait(&keys, start + 301) == UINT32_MAX);

    reset();
    assert(!edge(0, true, start));
    assert(!edge(0, false, start + 20));
    assert(!edge(0, true, start + 299));
    /* 第二击以按下时刻判窗；松手可以越过双击窗，但不能越过长按阈值。 */
    assert(edge(0, false, start + 400) == 1 && output[0].kind == IW_KEY_DOUBLE);
    assert(!iw_keys_advance(&keys, start + 1000, output));

    reset();
    assert(!edge(0, true, start));
    assert(!edge(0, false, start + 20));
    assert(edge(0, true, start + 300) == 1 && output[0].kind == IW_KEY_SINGLE);
    assert(!edge(0, false, start + 320));
    assert(iw_keys_advance(&keys, start + 600, output) == 1 && output[0].kind == IW_KEY_SINGLE);

    reset();
    assert(!edge(0, true, start));
    assert(!edge(0, false, start + 20));
    assert(!edge(0, true, start + 100));
    assert(iw_keys_wait(&keys, start + 899) == 1);
    assert(edge(0, false, start + 900) == 1 && output[0].kind == IW_KEY_HOLD);
    assert(!iw_keys_advance(&keys, start + 2000, output));

    reset();
    assert(!edge(1, true, start));
    assert(!edge(0, true, start + 700));
    assert(iw_keys_advance(&keys, start + 1500, output) == 2);
    assert(output[0].key == 0 && output[0].kind == IW_KEY_HOLD);
    assert(output[1].key == 1 && output[1].kind == IW_KEY_HOLD);
    assert(!edge(1, false, start + 1510));
    assert(!edge(0, false, start + 1511));
}

static void cancellation(void)
{
    reset();
    assert(!edge(0, true, 100));
    assert(!edge(1, true, 101));
    iw_keys_cancel(&keys);
    assert(!iw_keys_advance(&keys, 9000, output));
    assert(!edge(0, true, 9001));
    assert(!edge(1, false, 9002));
    assert(!edge(1, true, 9003));
    assert(!edge(1, false, 9004));
    assert(iw_keys_advance(&keys, 9284, output) == 1 && output[0].key == 1);
    assert(keys.keys[0].wait_release);
    iw_keys_recover(&keys, 0, false, 9300);
    iw_keys_recover(&keys, 0, true, 9310);
    iw_keys_recover(&keys, 0, false, 9320);
    iw_keys_recover(&keys, 0, false, 9339);
    assert(keys.keys[0].wait_release);
    iw_keys_recover(&keys, 0, false, 9340);
    assert(!keys.keys[0].wait_release);
    assert(!edge(0, true, 9341));
    /* 过期队列边沿使整个组合取消，不能用过去的松手释放新手势。 */
    assert(!edge(0, false, 100));
    assert(keys.keys[0].wait_release && keys.keys[1].wait_release);
}

static void arbitration(void)
{
    reset();
    iw_keys_water_lock(&keys, true);
    assert(!edge(0, false, 0));
    assert(!edge(0, true, 1));
    assert(!iw_keys_advance(&keys, 801, output));
    assert(iw_keys_advance(&keys, 2001, output) == 1 && output[0].kind == IW_KEY_UNLOCK);
    assert(iw_keys_intent(output[0], IW_INPUT_WATER) == IW_INTENT_UNLOCK);
    assert(!edge(0, false, 2002));
    for (unsigned context = 0; context <= IW_INPUT_RECOVERY; context++) {
        for (unsigned key = 0; key < IW_KEY_COUNT; key++) {
            for (unsigned kind = IW_KEY_SINGLE; kind <= IW_KEY_UNLOCK; kind++) {
                iw_key_signal_t signal = {key, (iw_key_kind_t)kind};
                iw_input_intent_t intent = iw_keys_intent(signal, (iw_input_context_t)context);
                if (context == IW_INPUT_LOCKED) assert(intent == IW_INTENT_NONE);
                if (context == IW_INPUT_WATER)
                    assert(intent == (key == 0 && kind == IW_KEY_UNLOCK ? IW_INTENT_UNLOCK : IW_INTENT_NONE));
                if (context == IW_INPUT_RECOVERY)
                    assert(intent == (key == 0 && kind == IW_KEY_SINGLE ? IW_INTENT_RECOVER : IW_INTENT_NONE));
                if (context == IW_INPUT_OVERLAY || context == IW_INPUT_ALERT)
                    assert(intent == (kind == IW_KEY_SINGLE ? IW_INTENT_DISMISS : IW_INTENT_NONE));
            }
        }
    }
    assert(iw_keys_intent((iw_key_signal_t){0, IW_KEY_SINGLE}, IW_INPUT_NORMAL) == IW_INTENT_HOME);
    assert(iw_keys_intent((iw_key_signal_t){1, IW_KEY_DOUBLE}, IW_INPUT_NORMAL) == IW_INTENT_WALLET);
    assert(iw_keys_intent((iw_key_signal_t){0, IW_KEY_HOLD}, IW_INPUT_NORMAL) == IW_INTENT_VOICE);
    iw_keys_rotate(&keys, INT32_MAX, false);
    assert(iw_keys_take_rotation(&keys, false) == IW_ROTATION_LIMIT);
    iw_keys_rotate(&keys, INT32_MIN, false);
    assert(iw_keys_take_rotation(&keys, false) == -IW_ROTATION_LIMIT);
    iw_keys_rotate(&keys, 10, false);
    iw_keys_rotate(&keys, 10, true);
    assert(!iw_keys_take_rotation(&keys, false));
    iw_keys_rotate(&keys, -5, false);
    iw_keys_cancel(&keys);
    assert(!iw_keys_take_rotation(&keys, false));
}

int main(void)
{
    assert(!iw_keys_init(NULL, &config));
    iw_keys_config_t bad = config;
    bad.double_ticks = UINT32_MAX;
    assert(!iw_keys_init(&keys, &bad));
    for (unsigned i = 0; i < 1000; i++) {
        timing(i * 3000u);
        timing(UINT32_MAX - i);
        cancellation();
        arbitration();
    }
    puts("D09 keys: 1000 timing/wrap, independent keys, cancellation/recovery, arbitration and rotation passes");
    return 0;
}
