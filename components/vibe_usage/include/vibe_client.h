#ifndef VIBE_CLIENT_H_
#define VIBE_CLIENT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "vibe_usage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIBE_ORIGIN_BYTES 256U
#define VIBE_API_KEY_BYTES 512U
#define VIBE_DEVICE_CODE_BYTES 512U
#define VIBE_USER_CODE_BYTES 64U
#define VIBE_VERIFICATION_URL_BYTES 1024U

typedef struct {
    const char *bootstrap_origin;
    const char *client_name;
    const char *hostname;
    vibe_timezone_t timezone;
    uint32_t generation;
    uint32_t config_revision;
    uint32_t refresh_seconds;
    size_t max_usage_body_bytes;
    uint32_t max_usage_buckets;
} vibe_config_t;

typedef struct {
    char device_code[VIBE_DEVICE_CODE_BYTES];
    char user_code[VIBE_USER_CODE_BYTES];
    char verification_uri[VIBE_VERIFICATION_URL_BYTES];
    char verification_uri_complete[VIBE_VERIFICATION_URL_BYTES];
    uint32_t expires_in;
    uint32_t interval;
    int64_t monotonic_deadline_seconds;
} vibe_device_code_t;

typedef enum {
    VIBE_AUTH_PENDING = 0,
    VIBE_AUTH_SUCCESS,
    VIBE_AUTH_DENIED,
    VIBE_AUTH_EXPIRED,
    VIBE_AUTH_ALREADY_DELIVERED,
    VIBE_AUTH_SLOW_DOWN,
    VIBE_AUTH_PROTOCOL_ERROR,
} vibe_auth_status_t;

typedef struct {
    vibe_auth_status_t status;
    uint32_t retry_after_seconds;
} vibe_auth_result_t;

esp_err_t vibe_init(const vibe_config_t *config);
esp_err_t vibe_request_device_code(vibe_device_code_t *result);
esp_err_t vibe_poll_device_code(const char *device_code,
                                vibe_auth_result_t *result);
esp_err_t vibe_fetch_today(vibe_usage_snapshot_t *snapshot);
esp_err_t vibe_begin_reconcile(bool include_today);
esp_err_t vibe_reconcile(vibe_usage_snapshot_t *snapshot);
bool vibe_reconcile_pending(void);
esp_err_t vibe_load_cached(vibe_usage_snapshot_t *snapshot);
esp_err_t vibe_snapshot(vibe_window_t window,
                        vibe_usage_snapshot_t *snapshot);
esp_err_t vibe_unlink(void);
esp_err_t vibe_factory_reset(void);
esp_err_t vibe_factory_reset_pending(bool *pending);
esp_err_t vibe_factory_reset_resume_local(void);
esp_err_t vibe_factory_reset_complete(void);
void vibe_cancel(void);
bool vibe_has_auth(void);
uint32_t vibe_account_generation(void);
vibe_error_t vibe_last_error(void);
int vibe_last_http_status(void);
uint32_t vibe_last_retry_after_seconds(void);
uint32_t vibe_retry_remaining_seconds(void);

#ifdef __cplusplus
}
#endif

#endif  // VIBE_CLIENT_H_
