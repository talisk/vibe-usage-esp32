#include "vibe_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls_errors.h"
#include "esp_timer.h"
#include "vibe_http_policy.h"

typedef struct {
    size_t received;
    size_t limit;
    vibe_http_data_cb_t callback;
    void *callback_context;
    vibe_error_t error;
    bool content_type_seen;
    bool content_type_json;
    bool deadline_expired;
    uint32_t expected_cancel_generation;
    uint32_t retry_after_seconds;
    int64_t deadline_us;
} http_event_context_t;

static bool header_is_identity(const char *value) {
    if (value == NULL) return false;
    while (*value == ' ' || *value == '\t') ++value;
    const size_t length = strlen(value);
    size_t end = length;
    while (end > 0 && (value[end - 1U] == ' ' || value[end - 1U] == '\t')) {
        --end;
    }
    return end == 8U && strncasecmp(value, "identity", 8U) == 0;
}

static esp_err_t http_event(esp_http_client_event_t *event) {
    http_event_context_t *context =
        (http_event_context_t *)event->user_data;
    if (context == NULL) return ESP_FAIL;
    if (atomic_load(&g_vibe.cancel_generation) !=
        context->expected_cancel_generation) {
        context->error = VIBE_ERR_CANCELLED;
        return ESP_FAIL;
    }
    if (esp_timer_get_time() > context->deadline_us) {
        context->deadline_expired = true;
        return ESP_FAIL;
    }
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key != NULL &&
        event->header_value != NULL) {
        if (strcasecmp(event->header_key, "Content-Encoding") == 0 &&
            !header_is_identity(event->header_value)) {
            context->error = VIBE_ERR_SCHEMA;
            return ESP_FAIL;
        }
        if (strcasecmp(event->header_key, "Content-Type") == 0) {
            const bool json =
                vibe_http_content_type_is_json(event->header_value);
            context->content_type_json =
                !context->content_type_seen && json;
            context->content_type_seen = true;
        } else if (strcasecmp(event->header_key, "Retry-After") == 0) {
            uint32_t delay = 0;
            if (vibe_http_parse_retry_after(event->header_value,
                                            (int64_t)time(NULL), &delay) &&
                delay > context->retry_after_seconds) {
                context->retry_after_seconds = delay;
            }
        }
    }
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t size = (size_t)event->data_len;
    if (context->received > context->limit ||
        size > context->limit - context->received) {
        context->error = VIBE_ERR_BODY_TOO_LARGE;
        return ESP_FAIL;
    }
    context->received += size;
    if (context->callback != NULL) {
        context->error = context->callback((const uint8_t *)event->data, size,
                                           context->callback_context);
        if (context->error != VIBE_OK) return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t vibe_http_request(const char *method, const char *url,
                            const char *authorization, const char *json_body,
                            size_t max_body_bytes,
                            vibe_http_data_cb_t data_callback,
                            void *data_context, int timeout_ms,
                            int *status_code, size_t *received_bytes,
                            vibe_error_t *body_error,
                            uint32_t *retry_after_seconds) {
    if (method == NULL || url == NULL || max_body_bytes == 0 ||
        timeout_ms <= 0 || status_code == NULL || received_bytes == NULL ||
        body_error == NULL || retry_after_seconds == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *status_code = 0;
    *received_bytes = 0;
    *body_error = VIBE_OK;
    *retry_after_seconds = 0;
    const int64_t started_us = esp_timer_get_time();
    http_event_context_t event_context = {
        .received = 0,
        .limit = max_body_bytes,
        .callback = data_callback,
        .callback_context = data_context,
        .error = VIBE_OK,
        .content_type_seen = false,
        .content_type_json = false,
        .deadline_expired = false,
        .expected_cancel_generation = atomic_load(&g_vibe.cancel_generation),
        .retry_after_seconds = 0,
        .deadline_us = started_us + (int64_t)timeout_ms * 1000LL,
    };
    const esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event,
        .user_data = &event_context,
        .timeout_ms = timeout_ms < 10000 ? timeout_ms : 10000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        *body_error = VIBE_ERR_NO_MEMORY;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t error = ESP_OK;
    if (strcmp(method, "POST") == 0) {
        error = esp_http_client_set_method(client, HTTP_METHOD_POST);
    } else if (strcmp(method, "GET") == 0) {
        error = esp_http_client_set_method(client, HTTP_METHOD_GET);
    } else {
        error = ESP_ERR_INVALID_ARG;
    }
    if (error == ESP_OK) {
        error = esp_http_client_set_header(client, "Accept", "application/json");
    }
    if (error == ESP_OK) {
        error = esp_http_client_set_header(client, "Accept-Encoding", "identity");
    }
    if (error == ESP_OK) {
        char user_agent[64];
        snprintf(user_agent, sizeof(user_agent), "vibe-usage-esp32/%s",
                 esp_app_get_description()->version);
        error = esp_http_client_set_header(client, "User-Agent",
                                           user_agent);
    }
    if (error == ESP_OK && authorization != NULL) {
        error = esp_http_client_set_header(client, "Authorization",
                                           authorization);
    }
    if (error == ESP_OK && json_body != NULL) {
        error = esp_http_client_set_header(client, "Content-Type",
                                           "application/json");
        if (error == ESP_OK) {
            error = esp_http_client_set_post_field(client, json_body,
                                                   (int)strlen(json_body));
        }
    }
    if (error == ESP_OK) error = esp_http_client_perform(client);

    *status_code = esp_http_client_get_status_code(client);
    *received_bytes = event_context.received;
    *retry_after_seconds = event_context.retry_after_seconds;
    if (error == ESP_OK &&
        !esp_http_client_is_complete_data_received(client)) {
        event_context.error = VIBE_ERR_SCHEMA;
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK && event_context.received > 0 &&
        (!event_context.content_type_seen || !event_context.content_type_json)) {
        event_context.error = VIBE_ERR_SCHEMA;
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error != ESP_OK && event_context.error == VIBE_OK) {
        if (error == ESP_ERR_NO_MEM) {
            event_context.error = VIBE_ERR_NO_MEMORY;
        } else {
            int tls_code = 0;
            int tls_flags = 0;
            const esp_err_t tls_error =
                esp_http_client_get_and_clear_last_tls_error(
                    client, &tls_code, &tls_flags);
            if (tls_error == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) {
                event_context.error = VIBE_ERR_DNS;
            } else if (tls_error >= ESP_ERR_ESP_TLS_BASE || tls_code != 0 ||
                       tls_flags != 0) {
                event_context.error = VIBE_ERR_TLS;
            } else {
                event_context.error = VIBE_ERR_NETWORK;
            }
        }
    }
    *body_error = event_context.error;
    esp_http_client_cleanup(client);
    if (event_context.deadline_expired) return ESP_ERR_TIMEOUT;
    if (event_context.error != VIBE_OK) return ESP_FAIL;
    return error;
}
