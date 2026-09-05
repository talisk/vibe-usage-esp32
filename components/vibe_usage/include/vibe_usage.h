#ifndef VIBE_USAGE_H_
#define VIBE_USAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIBE_SCHEMA_VERSION 1U
#define VIBE_CACHE_SCHEMA_VERSION 2U
#define VIBE_METRIC_ID_API_TOTAL_V1 1U
#define VIBE_MAX_AGENTS 12U
#define VIBE_CACHE_DAYS 8U
#define VIBE_SOURCE_SLOTS 24U
#define VIBE_SOURCE_ID_BYTES 48U
#define VIBE_TIMEZONE_NAME_BYTES 32U
#define VIBE_CACHE_BLOB_MAX_BYTES 4096U
#define VIBE_OTHER_SOURCE_ID "__other__"

typedef enum {
    VIBE_READY = 0,
    VIBE_EMPTY,
    VIBE_STALE,
    VIBE_AUTH_REQUIRED,
    VIBE_LINK_REQUIRED,
} vibe_data_state_t;

typedef enum {
    VIBE_OK = 0,
    VIBE_ERR_INVALID_ARGUMENT,
    VIBE_ERR_INVALID_JSON,
    VIBE_ERR_SCHEMA,
    VIBE_ERR_OVERFLOW,
    VIBE_ERR_BODY_TOO_LARGE,
    VIBE_ERR_TOO_MANY_BUCKETS,
    VIBE_ERR_OUT_OF_RANGE,
    VIBE_ERR_BUFFER_TOO_SMALL,
    VIBE_ERR_CRC,
    VIBE_ERR_VERSION,
    VIBE_ERR_CANCELLED,
    VIBE_ERR_NO_MEMORY,
    VIBE_ERR_STORAGE,
    VIBE_ERR_HTTP_STATUS,
    VIBE_ERR_DNS,
    VIBE_ERR_TLS,
    VIBE_ERR_NETWORK,
    VIBE_ERR_TIME_INVALID,
} vibe_error_t;

typedef enum {
    VIBE_TZ_ASIA_SHANGHAI = 0,
    VIBE_TZ_UTC = 1,
} vibe_timezone_t;

typedef enum {
    VIBE_WINDOW_TODAY = 0,
    VIBE_WINDOW_SEVEN_DAYS = 1,
} vibe_window_t;

typedef struct {
    char id[VIBE_SOURCE_ID_BYTES];
    uint64_t today_tokens;
    uint64_t seven_day_tokens;
    uint16_t today_bp;
    uint16_t seven_day_bp;
} vibe_agent_usage_t;

typedef struct {
    uint32_t schema_version;
    uint32_t generation;
    int32_t today_key;
    uint64_t today_tokens;
    uint64_t seven_day_tokens;
    vibe_agent_usage_t agents[VIBE_MAX_AGENTS];
    uint8_t agent_count;
    uint8_t valid_days_mask;
    bool today_complete;
    bool seven_day_complete;
    bool has_any_data;
    bool has_lkg;
    bool time_valid;
    bool persisted;
    bool sources_collapsed;
    int64_t last_fetch_at;
    int64_t last_reconcile_at;
    vibe_data_state_t state;
} vibe_usage_snapshot_t;

const char *vibe_error_name(vibe_error_t error);
const char *vibe_timezone_name(vibe_timezone_t timezone);
const char *vibe_timezone_posix(vibe_timezone_t timezone);

#ifdef __cplusplus
}
#endif

#endif  // VIBE_USAGE_H_
