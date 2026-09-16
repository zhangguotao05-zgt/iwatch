#ifndef IW_TIME_RTC_H
#define IW_TIME_RTC_H

#include <stdbool.h>
#include <stdint.h>

bool iw_time_rtc_available(void);
bool iw_time_rtc_read(uint32_t *utc_seconds, int16_t *offset_minutes);
bool iw_time_rtc_write(uint32_t utc_seconds, int16_t offset_minutes);
bool iw_time_rtc_next_session(uint32_t *session_id);

#endif
