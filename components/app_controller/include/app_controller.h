#ifndef APP_CONTROLLER_H_
#define APP_CONTROLLER_H_

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "vibe_client.h"
#include "vibe_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_INTENT_REFRESH = 0,
    APP_INTENT_RECONCILE,
    APP_INTENT_START_WIFI,
    APP_INTENT_RECONFIGURE_WIFI,
    APP_INTENT_RELINK,
    APP_INTENT_UNLINK,
    APP_INTENT_TOGGLE_TIMEZONE,
    APP_INTENT_CYCLE_LANGUAGE,
    APP_INTENT_CYCLE_ALERT_VOLUME,
    APP_INTENT_FACTORY_RESET,
    APP_INTENT_TODO_VOICE_START,
    APP_INTENT_LLM_CONFIG,
    APP_INTENT_LLM_CONFIG_CLOSE,
    APP_INTENT_TODO_ACK,
} app_intent_t;

typedef enum {
    APP_REASON_NONE = 0,
    APP_REASON_WIFI,
    APP_REASON_TIME,
    APP_REASON_AUTH,
    APP_REASON_HTTP,
    APP_REASON_SCHEMA,
    APP_REASON_STORAGE,
    APP_REASON_CANCELLED,
} app_reason_t;

typedef struct {
    uint32_t id;
    bool completed;
    char title[97];
    int64_t due_utc;
    uint32_t repeat_seconds;
} app_todo_item_view_t;

typedef struct {
    uint32_t revision;
    vibe_app_state_t lifecycle;
    vibe_data_state_t data_state;
    app_reason_t reason;
    bool busy;
    bool wifi_has_credentials;
    bool wifi_connected;
    bool wifi_provisioning;
    bool time_valid;
    bool has_auth;
    bool has_today;
    bool has_seven_day;
    uint8_t seven_day_coverage;
    uint32_t generation;
    uint32_t config_revision;
    uint32_t refresh_seconds;
    vibe_timezone_t timezone;
    vibe_language_t language;
    uint8_t alert_volume;
    int64_t now_utc;
    int64_t next_refresh_utc;
    int64_t link_deadline_monotonic;
    int64_t link_now_monotonic;
    vibe_error_t last_error;
    esp_err_t last_system_error;
    int last_http_status;
    char device_id[5];
    char ap_ssid[33];
    char portal_url[32];
    char user_code[VIBE_USER_CODE_BYTES];
    char verification_uri[VIBE_VERIFICATION_URL_BYTES];
    char detail[96];
    bool llm_config_open;
    bool llm_enabled;
    char llm_portal_url[128];
    char todo_status[96];
    bool todo_recording;
    bool todo_busy;
    uint32_t todo_revision;
    uint8_t todo_count;
    app_todo_item_view_t todos[12];
    uint32_t todo_alert_id;
    uint32_t todo_alert_sequence;
    esp_err_t nfc_error;
    vibe_usage_snapshot_t today;
    vibe_usage_snapshot_t seven_day;
} app_controller_view_t;

typedef void (*app_controller_changed_cb_t)(void *context);
typedef void (*app_controller_time_cb_t)(int64_t utc_seconds, void *context);

typedef struct {
    const char *bootstrap_origin;
    const char *client_name;
    const char *product_prefix;
    const char *hostname;
    device_config_t settings;
    app_controller_changed_cb_t changed_callback;
    void *changed_context;
    app_controller_time_cb_t time_synchronized_callback;
    void *time_context;
    uint32_t task_stack_bytes;
    UBaseType_t task_priority;
} app_controller_config_t;

esp_err_t app_controller_init(const app_controller_config_t *config);
esp_err_t app_controller_start(void);
esp_err_t app_controller_dispatch(app_intent_t intent);
esp_err_t app_controller_get_view(app_controller_view_t *view);
/* Non-blocking release signal, including while the network worker records. */
void app_controller_voice_stop(void);

/* Stop cloud work and Wi-Fi before a board-specific power-off sequence. */
esp_err_t app_controller_prepare_shutdown(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif  // APP_CONTROLLER_H_
