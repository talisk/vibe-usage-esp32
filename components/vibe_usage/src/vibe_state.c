#include "vibe_state.h"

#include <string.h>

void vibe_app_model_init(vibe_app_model_t *model) {
    if (model == NULL) return;
    memset(model, 0, sizeof(*model));
    model->lifecycle = VIBE_APP_BOOT;
    model->data_state = VIBE_LINK_REQUIRED;
    model->generation = 1;
    model->config_revision = 1;
    model->operation_revision = 1;
}

uint32_t vibe_app_reduce(vibe_app_model_t *model,
                         const vibe_app_event_t *event) {
    if (model == NULL || event == NULL) return VIBE_EFFECT_NONE;
    switch (event->type) {
        case VIBE_EVENT_BOOT_LOADED:
            model->has_wifi_credentials = event->flag;
            model->lifecycle = event->flag ? VIBE_APP_WIFI_CONNECTING
                                           : VIBE_APP_WIFI_REQUIRED;
            return event->flag ? VIBE_EFFECT_START_STATION : VIBE_EFFECT_NONE;
        case VIBE_EVENT_USER_START_WIFI:
        case VIBE_EVENT_USER_RECONFIGURE_WIFI:
            ++model->operation_revision;
            model->lifecycle = VIBE_APP_WIFI_PROVISIONING;
            return VIBE_EFFECT_CANCEL_NETWORK | VIBE_EFFECT_START_PORTAL;
        case VIBE_EVENT_WIFI_GOT_IP:
            model->lifecycle = VIBE_APP_TIME_SYNC;
            return VIBE_EFFECT_NONE;
        case VIBE_EVENT_WIFI_LOST:
            ++model->operation_revision;
            model->lifecycle = VIBE_APP_WIFI_CONNECTING;
            if (model->has_lkg) model->data_state = VIBE_STALE;
            return VIBE_EFFECT_CANCEL_NETWORK;
        case VIBE_EVENT_TIME_VALID:
            model->time_valid = true;
            if (model->has_auth) {
                model->lifecycle = VIBE_APP_SYNCING;
                return VIBE_EFFECT_FETCH_TODAY;
            }
            model->lifecycle = VIBE_APP_DEVICE_LINK;
            model->data_state = VIBE_LINK_REQUIRED;
            return VIBE_EFFECT_REQUEST_CODE;
        case VIBE_EVENT_TIME_INVALID:
            model->time_valid = false;
            model->lifecycle = VIBE_APP_TIME_REQUIRED;
            if (model->has_lkg) model->data_state = VIBE_STALE;
            return VIBE_EFFECT_CANCEL_NETWORK;
        case VIBE_EVENT_USER_RELINK:
            ++model->generation;
            ++model->operation_revision;
            model->has_auth = false;
            model->has_lkg = false;
            model->data_state = VIBE_LINK_REQUIRED;
            model->lifecycle = model->time_valid ? VIBE_APP_DEVICE_LINK
                                                 : VIBE_APP_TIME_SYNC;
            return VIBE_EFFECT_CANCEL_NETWORK | VIBE_EFFECT_PERSIST_TOMBSTONE |
                   VIBE_EFFECT_INVALIDATE_CACHE |
                   (model->time_valid ? VIBE_EFFECT_REQUEST_CODE : 0U);
        case VIBE_EVENT_AUTH_SUCCESS:
            if (event->generation != model->generation ||
                event->config_revision != model->config_revision) {
                return VIBE_EFFECT_DISCARD_RESULT;
            }
            model->has_auth = true;
            model->lifecycle = VIBE_APP_SYNCING;
            return VIBE_EFFECT_PERSIST_AUTH | VIBE_EFFECT_FETCH_TODAY;
        case VIBE_EVENT_HTTP_SUCCESS:
            if (event->generation != model->generation ||
                event->config_revision != model->config_revision) {
                return VIBE_EFFECT_DISCARD_RESULT;
            }
            model->has_lkg = true;
            model->data_state = event->flag ? VIBE_EMPTY : VIBE_READY;
            model->lifecycle = VIBE_APP_DASHBOARD;
            return VIBE_EFFECT_NONE;
        case VIBE_EVENT_HTTP_401:
            ++model->generation;
            ++model->operation_revision;
            model->has_auth = false;
            model->has_lkg = false;
            model->data_state = VIBE_AUTH_REQUIRED;
            model->lifecycle = VIBE_APP_DASHBOARD;
            return VIBE_EFFECT_CANCEL_NETWORK | VIBE_EFFECT_PERSIST_TOMBSTONE |
                   VIBE_EFFECT_INVALIDATE_CACHE;
        case VIBE_EVENT_HTTP_FAILURE:
            model->data_state = VIBE_STALE;
            model->lifecycle = VIBE_APP_DASHBOARD;
            return VIBE_EFFECT_NONE;
        case VIBE_EVENT_TIMEZONE_CHANGED:
            ++model->config_revision;
            ++model->operation_revision;
            model->has_lkg = false;
            model->data_state = VIBE_STALE;
            model->lifecycle = model->has_auth && model->time_valid
                                   ? VIBE_APP_SYNCING
                                   : VIBE_APP_TIME_SYNC;
            return VIBE_EFFECT_CANCEL_NETWORK | VIBE_EFFECT_INVALIDATE_CACHE |
                   (model->has_auth && model->time_valid
                        ? VIBE_EFFECT_FETCH_TODAY
                        : 0U);
        case VIBE_EVENT_USER_RESET:
            ++model->operation_revision;
            model->lifecycle = VIBE_APP_RESETTING;
            return VIBE_EFFECT_CANCEL_NETWORK | VIBE_EFFECT_STOP_WORKERS |
                   VIBE_EFFECT_WRITE_RESET_JOURNAL;
        case VIBE_EVENT_POWER_SLEEP:
            ++model->operation_revision;
            model->lifecycle = VIBE_APP_PREPARE_SLEEP;
            return VIBE_EFFECT_CANCEL_NETWORK | VIBE_EFFECT_STOP_WORKERS;
        case VIBE_EVENT_STORAGE_FAILURE:
            model->lifecycle = VIBE_APP_STORAGE_ERROR;
            return VIBE_EFFECT_CANCEL_NETWORK;
        default:
            return VIBE_EFFECT_NONE;
    }
}
