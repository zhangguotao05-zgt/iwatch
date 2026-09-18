#include "iw_render_probe.h"

static iw_render_sample_t samples[IW_RENDER_PROBE_CAPACITY], current;
static unsigned count, pending;
static bool recording, collecting, drawn;

void iw_render_probe_start(void) {
    count = pending = 0;
    recording = true;
    collecting = drawn = false;
}
void iw_render_probe_stop(void) { recording = collecting = false; }
bool iw_render_probe_active(void) { return recording; }

static void observe_idle(uint32_t now, bool idle) {
    if (!idle) return;
    for (; pending < count; pending++) {
        samples[pending].idle_ms = now;
        samples[pending].idle_seen = true;
    }
}
void iw_render_probe_begin(uint32_t now, bool idle) {
    if (!recording) return;
    observe_idle(now, idle);
    collecting = count < IW_RENDER_PROBE_CAPACITY;
    drawn = false;
    current = (iw_render_sample_t){.begin_ms = now};
}
void iw_render_probe_draw(uint16_t page, uint32_t first_ms, bool first) {
    if (!recording || !collecting || drawn) return;
    drawn = true;
    current.page_id = page;
    current.first = first;
    current.first_ms = first_ms;
}
void iw_render_probe_end(uint32_t now, bool idle) {
    if (!recording) return;
    if (collecting && drawn) {
        current.submit_ms = now;
        samples[count++] = current;
    }
    observe_idle(now, idle);
    collecting = drawn = false;
}
const iw_render_sample_t *iw_render_probe_samples(unsigned *output_count) {
    if (!output_count || recording) return 0;
    *output_count = count;
    return samples;
}
