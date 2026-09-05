#ifndef VIBE_INTERNAL_H_
#define VIBE_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>

#include "vibe_cache.h"
#include "vibe_client.h"

typedef struct {
    bool present;
    bool tombstone;
    uint32_t generation;
    char origin[VIBE_ORIGIN_BYTES];
    char api_key[VIBE_API_KEY_BYTES];
} vibe_auth_record_t;

typedef struct {
    bool initialized;
    char bootstrap_origin[VIBE_ORIGIN_BYTES];
    char client_name[65];
    char hostname[65];
    vibe_timezone_t timezone;
    uint32_t config_revision;
    uint32_t refresh_seconds;
    size_t max_usage_body_bytes;
    uint32_t max_usage_buckets;
    _Atomic uint32_t cancel_generation;
    vibe_error_t last_error;
    int last_http_status;
    uint32_t last_retry_after_seconds;
    int64_t retry_not_before_utc;
    int64_t last_cache_write_at;
    vibe_auth_record_t auth;
    vibe_cache_t cache;
} vibe_context_t;

extern vibe_context_t g_vibe;

typedef vibe_error_t (*vibe_http_data_cb_t)(const uint8_t *data, size_t size,
                                           void *context);

esp_err_t vibe_http_request(const char *method, const char *url,
                            const char *authorization, const char *json_body,
                            size_t max_body_bytes,
                            vibe_http_data_cb_t data_callback,
                            void *data_context, int timeout_ms,
                            int *status_code, size_t *received_bytes,
                            vibe_error_t *body_error,
                            uint32_t *retry_after_seconds);

esp_err_t vibe_store_load_auth(vibe_auth_record_t *record);
esp_err_t vibe_store_write_auth(const vibe_auth_record_t *record);
esp_err_t vibe_store_load_cache(vibe_cache_t *cache,
                                const vibe_auth_record_t *auth,
                                uint32_t config_revision,
                                vibe_timezone_t timezone);
esp_err_t vibe_store_write_cache(vibe_cache_t *cache);
esp_err_t vibe_store_clear_cache(void);
esp_err_t vibe_store_begin_reset(void);
esp_err_t vibe_store_finish_reset(void);
esp_err_t vibe_store_reset_pending(bool *pending);
esp_err_t vibe_store_complete_reset(void);
esp_err_t vibe_store_load_retry_not_before(int64_t *utc_seconds);
esp_err_t vibe_store_write_retry_not_before(int64_t utc_seconds);
esp_err_t vibe_store_clear_retry_not_before(void);

#endif  // VIBE_INTERNAL_H_
