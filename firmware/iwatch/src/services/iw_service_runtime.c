#include "iw_service_runtime.h"

#include <limits.h>
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#include "iw_gui_port.h"
#include "iw_time_rtc.h"

#define IW_SERVICE_THREAD_STACK_SIZE 4096u
#define IW_SERVICE_THREAD_PRIORITY 17u
#define IW_SERVICE_THREAD_TIMESLICE 10u
#define IW_SERVICE_EVENT_COMMAND 1u
#define IW_SERVICE_BATCH_LIMIT 8u
#define IW_DIAGNOSTIC_PAGE_ID 0xFFFEu

static iw_time_state_t runtime_time;
static iw_service_t runtime_service;
static struct rt_mutex runtime_mutex;
static struct rt_event runtime_event;
static struct rt_thread runtime_thread;
static bool runtime_initialized;

ALIGN(RT_ALIGN_SIZE)
static uint8_t runtime_thread_stack[IW_SERVICE_THREAD_STACK_SIZE];

static bool runtime_lock(void)
{
    return runtime_initialized && rt_mutex_take(&runtime_mutex, RT_WAITING_FOREVER) == RT_EOK;
}

static void runtime_unlock(void)
{
    (void)rt_mutex_release(&runtime_mutex);
}

static void runtime_sample_locked(void)
{
    iw_time_sample(&runtime_time, (uint32_t)rt_tick_get());
}

static bool runtime_has_pending(void)
{
    iw_service_stats_t stats;
    bool pending = false;

    if (!runtime_lock()) return false;
    iw_service_stats_read(&runtime_service, &stats);
    pending = stats.queue_depth != 0u;
    runtime_unlock();
    return pending;
}

static void runtime_process_batch(void)
{
    for (unsigned count = 0; count < IW_SERVICE_BATCH_LIMIT; count++)
    {
        iw_service_work_t work;
        iw_take_status_t take;
        iw_set_clock_payload_t payload;
        bool device_success;

        if (!runtime_lock()) return;
        runtime_sample_locked();
        take = iw_service_take_next(&runtime_service, &work);
        runtime_unlock();

        if (take == IW_TAKE_EMPTY) break;
        if (take == IW_TAKE_COMPLETED)
        {
            iw_gui_wake(IW_GUI_WAKE_STATE);
            continue;
        }

        /* RTC 控制可能阻塞，因此绝不在模型互斥锁内调用。 */
        device_success = iw_command_decode_set_clock(&work.command, &payload) &&
                         iw_time_rtc_write(payload.utc_seconds, payload.offset_minutes);

        if (!runtime_lock()) return;
        (void)iw_service_finish_set_clock(&runtime_service, &work.token,
                                          (uint32_t)rt_tick_get(), device_success);
        if (device_success)
        {
            (void)iw_service_set_capability(&runtime_service, IW_CAP_CLOCK,
                                            IW_CAP_STATE_AVAILABLE, 0);
            (void)iw_service_set_capability(&runtime_service, IW_CAP_RTC_BACKUP,
                                            IW_CAP_STATE_AVAILABLE, 0);
        }
        else
            (void)iw_service_set_capability(&runtime_service, IW_CAP_RTC_BACKUP,
                                            iw_time_rtc_available() ? IW_CAP_STATE_FAULT : IW_CAP_STATE_ABSENT,
                                            -RT_ERROR);
        runtime_unlock();
        iw_gui_wake(IW_GUI_WAKE_STATE);
    }

    if (runtime_has_pending()) (void)rt_event_send(&runtime_event, IW_SERVICE_EVENT_COMMAND);
}

static void runtime_thread_entry(void *parameter)
{
    (void)parameter;
    while (true)
    {
        rt_uint32_t events = 0;
        rt_int32_t sample_ticks = (rt_int32_t)rt_tick_from_millisecond(1000u);

        (void)rt_event_recv(&runtime_event, IW_SERVICE_EVENT_COMMAND,
                            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                            sample_ticks > 0 ? sample_ticks : 1, &events);
        runtime_process_batch();
        if (runtime_lock())
        {
            runtime_sample_locked();
            runtime_unlock();
        }
    }
}

int iw_service_runtime_init(void)
{
    uint32_t rtc_seconds = 0u;
    uint32_t session_id = 0u;
    int16_t rtc_offset_minutes = 0;
    bool rtc_present;
    bool rtc_valid;
    rt_err_t result;

    if (runtime_initialized) return RT_EOK;
    if (!iw_time_rtc_next_session(&session_id)) return -RT_EFULL;

    rtc_present = iw_time_rtc_available();
    rtc_valid = rtc_present && iw_time_rtc_read(&rtc_seconds, &rtc_offset_minutes) &&
                iw_time_utc_seconds_valid(rtc_seconds);
    if (iw_time_init(&runtime_time, (uint32_t)rt_tick_get(), RT_TICK_PER_SECOND,
                      rtc_valid ? rtc_seconds : 0,
                      rtc_valid ? rtc_offset_minutes : 0,
                      rtc_valid ? IW_TIME_SOURCE_RTC : IW_TIME_SOURCE_NONE) != IW_TIME_OK)
        return -RT_ERROR;
    if (!iw_service_init(&runtime_service, &runtime_time, session_id))
        return -RT_ERROR;
    (void)iw_service_set_capability(&runtime_service, IW_CAP_CLOCK,
                                    rtc_valid ? IW_CAP_STATE_AVAILABLE : IW_CAP_STATE_DEGRADED,
                                    rtc_valid ? 0 : -RT_ETIMEOUT);
    (void)iw_service_set_capability(&runtime_service, IW_CAP_RTC_BACKUP,
                                    rtc_present ? (rtc_valid ? IW_CAP_STATE_AVAILABLE : IW_CAP_STATE_DEGRADED)
                                                : IW_CAP_STATE_ABSENT,
                                    rtc_valid ? 0 : -RT_ERROR);

    result = rt_mutex_init(&runtime_mutex, "iw_svc", RT_IPC_FLAG_PRIO);
    if (result != RT_EOK) return result;
    result = rt_event_init(&runtime_event, "iw_sevt", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        (void)rt_mutex_detach(&runtime_mutex);
        return result;
    }
    result = rt_thread_init(&runtime_thread, "iw_service", runtime_thread_entry, RT_NULL,
                            runtime_thread_stack, sizeof(runtime_thread_stack),
                            IW_SERVICE_THREAD_PRIORITY, IW_SERVICE_THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        (void)rt_event_detach(&runtime_event);
        (void)rt_mutex_detach(&runtime_mutex);
        return result;
    }
    runtime_initialized = true;
    result = rt_thread_startup(&runtime_thread);
    if (result != RT_EOK)
    {
        runtime_initialized = false;
        (void)rt_thread_detach(&runtime_thread);
        (void)rt_event_detach(&runtime_event);
        (void)rt_mutex_detach(&runtime_mutex);
    }
    return result;
}

uint32_t iw_service_current_session(void)
{
    uint32_t session = 0u;
    if (!runtime_lock()) return 0u;
    session = iw_service_session_id(&runtime_service);
    runtime_unlock();
    return session;
}

iw_submit_status_t iw_command_submit(const iw_command_t *command)
{
    iw_submit_status_t status;

    if (!runtime_lock()) return IW_SUBMIT_BUSY_NO_ADMISSION;
    status = iw_service_command_submit(&runtime_service, command);
    runtime_unlock();
    if (status == IW_SUBMIT_QUEUED) (void)rt_event_send(&runtime_event, IW_SERVICE_EVENT_COMMAND);
    return status;
}

iw_result_lookup_t iw_result_get(uint32_t session_id, uint32_t request_id, iw_result_t *result)
{
    iw_result_lookup_t status;

    if (!runtime_lock()) return IW_RESULT_LOOKUP_INVALID;
    status = iw_service_result_get(&runtime_service, session_id, request_id, result);
    runtime_unlock();
    return status;
}

iw_ack_status_t iw_result_ack(const iw_result_token_t *token)
{
    iw_ack_status_t status;

    if (!runtime_lock()) return IW_ACK_INVALID;
    status = iw_service_result_ack(&runtime_service, token);
    runtime_unlock();
    return status;
}

iw_snapshot_status_t iw_snapshot_read(iw_snapshot_topic_t topic,
                                      void *output,
                                      size_t capacity,
                                      size_t *required)
{
    iw_snapshot_status_t status;

    if (!runtime_lock()) return IW_SNAPSHOT_INVALID;
    runtime_sample_locked();
    status = iw_service_snapshot_read(&runtime_service, topic, output, capacity, required);
    runtime_unlock();
    return status;
}

bool iw_clock_read(iw_clock_snapshot_t *snapshot)
{
    bool success;

    if (!snapshot || !runtime_lock()) return false;
    runtime_sample_locked();
    success = iw_time_read(&runtime_time, snapshot);
    runtime_unlock();
    return success;
}

void iw_service_runtime_stats(iw_service_stats_t *stats)
{
    if (!stats || !runtime_lock()) return;
    iw_service_stats_read(&runtime_service, stats);
    runtime_unlock();
}

static void iw_clock_stat(void)
{
    iw_clock_snapshot_t clock;

    if (!iw_clock_read(&clock))
    {
        rt_kprintf("clock unavailable\n");
        return;
    }
    rt_kprintf("clock mono_ms=%llu utc_ms=%lld valid=%u offset_min=%d revision=%u source=%u\n",
               (unsigned long long)clock.mono_ms, (long long)clock.utc_ms,
               (unsigned)clock.valid, (int)clock.offset_minutes,
               (unsigned)clock.revision, (unsigned)clock.source);
}
MSH_CMD_EXPORT(iw_clock_stat, Show versioned clock snapshot);

static void iw_service_stat(void)
{
    iw_service_stats_t stats;

    memset(&stats, 0, sizeof(stats));
    iw_service_runtime_stats(&stats);
    rt_kprintf("service session=%u submitted=%u duplicate=%u rejected=%u completed=%u ack=%u\n",
               (unsigned)iw_service_current_session(), (unsigned)stats.submitted,
               (unsigned)stats.duplicates, (unsigned)stats.rejected,
               (unsigned)stats.completed, (unsigned)stats.acknowledged);
    rt_kprintf("service queue=%u active=%u ledger=%u high=%u revision=%u\n",
               (unsigned)stats.queue_depth, (unsigned)stats.active_count,
               (unsigned)stats.ledger_used, (unsigned)stats.accepted_request_high_water,
               (unsigned)stats.service_revision);
}
MSH_CMD_EXPORT(iw_service_stat, Show command and result ledger statistics);

static iw_client_session_t diagnostic_client;
static uint32_t diagnostic_last_request;

static int iw_clock_set(int argc, char **argv)
{
    iw_clock_snapshot_t clock;
    iw_command_t command;
    uint32_t session;
    uint32_t request;
    unsigned long utc_seconds;
    long offset_minutes;
    char *utc_end;
    char *offset_end;
    iw_submit_status_t status;

    if (argc != 3)
    {
        rt_kprintf("usage: iw_clock_set <utc_seconds> <offset_minutes>\n");
        return -RT_EINVAL;
    }
    utc_seconds = strtoul(argv[1], &utc_end, 0);
    offset_minutes = strtol(argv[2], &offset_end, 0);
    if (utc_end == argv[1] || *utc_end != '\0' || offset_end == argv[2] || *offset_end != '\0' ||
            utc_seconds > UINT32_MAX || offset_minutes < INT16_MIN || offset_minutes > INT16_MAX ||
            !iw_clock_read(&clock))
        return -RT_EINVAL;

    session = iw_service_current_session();
    if (diagnostic_client.session_id != session && !iw_client_session_init(&diagnostic_client, session))
        return -RT_ERROR;
    if (!iw_client_next_request(&diagnostic_client, &request)) return -RT_EFULL;
    iw_command_init(&command, session, request, IW_DIAGNOSTIC_PAGE_ID, 1u, IW_OPCODE_SET_CLOCK);
    if (!iw_command_encode_set_clock(&command, (uint32_t)utc_seconds,
                                     (int16_t)offset_minutes, clock.revision))
        return -RT_EINVAL;
    status = iw_command_submit(&command);
    if (status == IW_SUBMIT_QUEUED || status == IW_SUBMIT_DUPLICATE)
        diagnostic_last_request = request;
    rt_kprintf("clock request=%u submit=%u expected_revision=%u\n",
               (unsigned)request, (unsigned)status, (unsigned)clock.revision);
    return status == IW_SUBMIT_QUEUED || status == IW_SUBMIT_DUPLICATE ? RT_EOK : -RT_ERROR;
}
MSH_CMD_EXPORT(iw_clock_set, Set clock through the service command path);

static void iw_clock_result(void)
{
    iw_result_t result;
    iw_result_token_t token;
    uint32_t session = iw_service_current_session();
    iw_result_lookup_t lookup = iw_result_get(session, diagnostic_last_request, &result);

    if (lookup != IW_RESULT_LOOKUP_FOUND)
    {
        rt_kprintf("clock result request=%u lookup=%u\n",
                   (unsigned)diagnostic_last_request, (unsigned)lookup);
        return;
    }
    rt_kprintf("clock result request=%u state=%u code=%u revision=%u mono_ms=%llu\n",
               (unsigned)result.request_id, (unsigned)result.state, (unsigned)result.code,
               (unsigned)result.model_revision, (unsigned long long)result.completed_mono_ms);
    if (result.state == IW_RESULT_STATE_TERMINAL)
    {
        token.session_id = result.session_id;
        token.request_id = result.request_id;
        token.ledger_generation = result.ledger_generation;
        rt_kprintf("clock result ack=%u\n", (unsigned)iw_result_ack(&token));
    }
}
MSH_CMD_EXPORT(iw_clock_result, Show and acknowledge the last diagnostic clock result);
