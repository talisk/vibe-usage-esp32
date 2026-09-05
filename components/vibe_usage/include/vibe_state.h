#ifndef VIBE_STATE_H_
#define VIBE_STATE_H_

#include <stdbool.h>
#include <stdint.h>

#include "vibe_usage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIBE_APP_BOOT = 0,
    VIBE_APP_LOAD_CONFIG,
    VIBE_APP_WIFI_REQUIRED,
    VIBE_APP_WIFI_PROVISIONING,
    VIBE_APP_WIFI_CONNECTING,
    VIBE_APP_TIME_SYNC,
    VIBE_APP_TIME_REQUIRED,
    VIBE_APP_DEVICE_LINK,
    VIBE_APP_SYNCING,
    VIBE_APP_DASHBOARD,
    VIBE_APP_SETTINGS,
    VIBE_APP_STORAGE_ERROR,
    VIBE_APP_PREPARE_SLEEP,
    VIBE_APP_RESETTING,
} vibe_app_state_t;

typedef enum {
    VIBE_EVENT_BOOT_LOADED = 0,
    VIBE_EVENT_USER_START_WIFI,
    VIBE_EVENT_USER_RECONFIGURE_WIFI,
    VIBE_EVENT_WIFI_GOT_IP,
    VIBE_EVENT_WIFI_LOST,
    VIBE_EVENT_TIME_VALID,
    VIBE_EVENT_TIME_INVALID,
    VIBE_EVENT_USER_RELINK,
    VIBE_EVENT_AUTH_SUCCESS,
    VIBE_EVENT_HTTP_SUCCESS,
    VIBE_EVENT_HTTP_401,
    VIBE_EVENT_HTTP_FAILURE,
    VIBE_EVENT_TIMEZONE_CHANGED,
    VIBE_EVENT_USER_RESET,
    VIBE_EVENT_POWER_SLEEP,
    VIBE_EVENT_STORAGE_FAILURE,
} vibe_app_event_type_t;

typedef struct {
    vibe_app_event_type_t type;
    uint32_t generation;
    uint32_t config_revision;
    bool flag;
} vibe_app_event_t;

enum {
    VIBE_EFFECT_NONE = 0,
    VIBE_EFFECT_CANCEL_NETWORK = 1U << 0,
    VIBE_EFFECT_START_STATION = 1U << 1,
    VIBE_EFFECT_START_PORTAL = 1U << 2,
    VIBE_EFFECT_REQUEST_CODE = 1U << 3,
    VIBE_EFFECT_PERSIST_AUTH = 1U << 4,
    VIBE_EFFECT_PERSIST_TOMBSTONE = 1U << 5,
    VIBE_EFFECT_FETCH_TODAY = 1U << 6,
    VIBE_EFFECT_INVALIDATE_CACHE = 1U << 7,
    VIBE_EFFECT_WRITE_RESET_JOURNAL = 1U << 8,
    VIBE_EFFECT_STOP_WORKERS = 1U << 9,
    VIBE_EFFECT_DISCARD_RESULT = 1U << 10,
};

typedef struct {
    vibe_app_state_t lifecycle;
    vibe_data_state_t data_state;
    uint32_t generation;
    uint32_t config_revision;
    uint32_t operation_revision;
    bool has_wifi_credentials;
    bool has_auth;
    bool time_valid;
    bool has_lkg;
} vibe_app_model_t;

void vibe_app_model_init(vibe_app_model_t *model);
uint32_t vibe_app_reduce(vibe_app_model_t *model,
                         const vibe_app_event_t *event);

#ifdef __cplusplus
}
#endif

#endif  // VIBE_STATE_H_
