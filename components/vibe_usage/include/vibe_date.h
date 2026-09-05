#ifndef VIBE_DATE_H_
#define VIBE_DATE_H_

#include <stdbool.h>
#include <stdint.h>

#include "vibe_usage.h"

#ifdef __cplusplus
extern "C" {
#endif

bool vibe_date_is_valid(int32_t date_key);
vibe_error_t vibe_date_add_days(int32_t date_key, int delta_days,
                                int32_t *result);
vibe_error_t vibe_date_from_epoch(int64_t epoch_seconds,
                                  vibe_timezone_t timezone,
                                  int32_t *date_key);
vibe_error_t vibe_iso8601_to_epoch(const char *text, int64_t *epoch_seconds);
vibe_error_t vibe_iso8601_to_date(const char *text,
                                  vibe_timezone_t timezone,
                                  int32_t *date_key);
vibe_error_t vibe_date_format_iso(int32_t date_key, char output[11]);
/* UTC instants for the API's inclusive [from, to] millisecond range. */
vibe_error_t vibe_date_api_bounds(int32_t date_key, vibe_timezone_t timezone,
                                  char from[25], char to[25]);

#ifdef __cplusplus
}
#endif

#endif  // VIBE_DATE_H_
