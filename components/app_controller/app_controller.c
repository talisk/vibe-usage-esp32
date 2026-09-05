#include "app_controller.h"
#include "vibe_product.h"
#include "esp_app_desc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "vibe_client.h"
#include "wifi_adapter.h"

#define APP_QUEUE_DEPTH 20U
#define APP_TASK_STACK_DEFAULT 12288U
#define APP_TASK_PRIORITY_DEFAULT 5U
#define APP_TIME_MIN_VALID 1704067200LL
#define APP_SHUTDOWN_READY_BIT (1U << 0)

static const char *TAG = "vibe_controller";

typedef enum {
    COMMAND_BOOT = 0,
    COMMAND_WIFI_EVENT,
    COMMAND_INTENT,
    COMMAND_SHUTDOWN,
} command_type_t;

typedef struct {
    command_type_t type;
    union {
        wifi_adapter_event_t wifi_event;
        app_intent_t intent;
    } value;
} command_t;

typedef struct {
    bool initialized;
    bool started;
    bool online;
    bool reconcile_active;
    bool request_code_retry;
    bool manual_refresh_pending;
    bool time_sync_pending;
    uint8_t failure_count;
    uint32_t link_interval_seconds;
    int64_t next_link_poll_us;
    int64_t next_refresh_us;
    int64_t next_reconcile_us;
    int64_t next_retry_us;
    int64_t last_manual_refresh_us;
    QueueHandle_t queue;
    SemaphoreHandle_t view_mutex;
    EventGroupHandle_t events;
    TaskHandle_t task;
    char bootstrap_origin[VIBE_ORIGIN_BYTES];
    char client_name[65];
    char product_prefix[24];
    char hostname[65];
    app_controller_config_t config;
    app_controller_view_t view;
    /* Only the controller task mutates this scratch copy. Keeping it in the
     * controller state avoids placing the full dashboard model on its stack. */
    app_controller_view_t scratch_view;
    vibe_device_code_t device_code;
} controller_t;

static controller_t s_controller;

static int64_t monotonic_us(void) { return esp_timer_get_time(); }

static bool system_time_valid(void) {
    return (int64_t)time(NULL) >= APP_TIME_MIN_VALID;
}

static bool copy_text(char *output, size_t output_size, const char *input) {
    if (output == NULL || output_size == 0 || input == NULL) return false;
    const size_t length = strlen(input);
    if (length >= output_size) return false;
    memcpy(output, input, length + 1U);
    return true;
}

static void notify_changed(void) {
    app_controller_changed_cb_t callback = s_controller.config.changed_callback;
    if (callback != NULL) callback(s_controller.config.changed_context);
}

static void publish_view(const app_controller_view_t *next) {
    if (next == NULL || s_controller.view_mutex == NULL) return;
    if (xSemaphoreTake(s_controller.view_mutex, portMAX_DELAY) != pdTRUE) return;
    const app_controller_view_t *previous = &s_controller.view;
    const bool diagnostic_changed =
        previous->lifecycle != next->lifecycle ||
        previous->has_auth != next->has_auth ||
        previous->last_http_status != next->last_http_status ||
        previous->last_error != next->last_error ||
        previous->last_system_error != next->last_system_error ||
        previous->seven_day_coverage != next->seven_day_coverage ||
        previous->today.persisted != next->today.persisted ||
        previous->seven_day.persisted != next->seven_day.persisted;
    s_controller.view = *next;
    ++s_controller.view.revision;
    xSemaphoreGive(s_controller.view_mutex);
    if (diagnostic_changed) {
        /* Only protocol/health metadata: never log view strings or totals. */
        ESP_LOGI(TAG,
                 "state=%u auth=%u http=%d error=%s system=0x%x "
                 "today_complete=%u coverage=%u/7 persisted=%u/%u "
                 "heap_internal=%u largest_internal=%u stack_free=%u",
                 (unsigned)next->lifecycle, (unsigned)next->has_auth,
                 next->last_http_status, vibe_error_name(next->last_error),
                 (unsigned)next->last_system_error,
                 (unsigned)next->today.today_complete,
                 (unsigned)next->seven_day_coverage,
                 (unsigned)next->today.persisted,
                 (unsigned)next->seven_day.persisted,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    notify_changed();
}

static void read_view(app_controller_view_t *view) {
    if (view == NULL) return;
    memset(view, 0, sizeof(*view));
    if (s_controller.view_mutex != NULL &&
        xSemaphoreTake(s_controller.view_mutex, portMAX_DELAY) == pdTRUE) {
        *view = s_controller.view;
        xSemaphoreGive(s_controller.view_mutex);
    }
}

static void set_detail(app_controller_view_t *view, const char *detail) {
    if (!copy_text(view->detail, sizeof(view->detail),
                   detail == NULL ? "" : detail)) {
        view->detail[0] = '\0';
    }
}

static uint8_t bit_count7(uint8_t value) {
    uint8_t count = 0;
    value &= 0x7fU;
    while (value != 0) {
        count = (uint8_t)(count + (value & 1U));
        value >>= 1U;
    }
    return count;
}

static uint8_t reconciliation_minute(void) {
    uint32_t hash = 2166136261U;
    for (const unsigned char *cursor =
             (const unsigned char *)s_controller.config.settings.device_id;
         *cursor != '\0'; ++cursor) {
        hash ^= *cursor;
        hash *= 16777619U;
    }
    return (uint8_t)(hash % 60U);
}

static bool daily_reconciliation_due(const app_controller_view_t *view) {
    if (view == NULL || !system_time_valid()) return false;
    const time_t now = time(NULL);
    struct tm current = {0};
    if (localtime_r(&now, &current) == NULL) return false;
    const uint8_t minute = reconciliation_minute();
    const bool window_reached = current.tm_hour > 3 ||
        (current.tm_hour == 3 && current.tm_min >= minute);
    if (view->seven_day.last_reconcile_at <= 0) return window_reached;
    if ((int64_t)now - view->seven_day.last_reconcile_at >= 86400LL) {
        return true;
    }
    const time_t previous = (time_t)view->seven_day.last_reconcile_at;
    struct tm prior = {0};
    if (localtime_r(&previous, &prior) == NULL) return false;
    return window_reached &&
        (current.tm_year != prior.tm_year || current.tm_yday != prior.tm_yday);
}

static void update_clock_fields(app_controller_view_t *view) {
    view->time_valid = system_time_valid();
    view->now_utc = view->time_valid ? (int64_t)time(NULL) : 0;
    view->link_now_monotonic = monotonic_us() / 1000000LL;
    if (s_controller.next_refresh_us > 0 && view->time_valid) {
        int64_t delta = (s_controller.next_refresh_us - monotonic_us()) /
                        1000000LL;
        if (delta < 0) delta = 0;
        view->next_refresh_utc = view->now_utc + delta;
    } else {
        view->next_refresh_utc = 0;
    }
}

static void clear_public_link(app_controller_view_t *view) {
    memset(&s_controller.device_code, 0, sizeof(s_controller.device_code));
    view->user_code[0] = '\0';
    view->verification_uri[0] = '\0';
    view->link_deadline_monotonic = 0;
    s_controller.next_link_poll_us = 0;
    s_controller.link_interval_seconds = 0;
    s_controller.next_refresh_us = 0;
    s_controller.next_reconcile_us = 0;
    s_controller.next_retry_us = 0;
    s_controller.reconcile_active = false;
    s_controller.request_code_retry = false;
    s_controller.manual_refresh_pending = false;
}

static void update_snapshots(app_controller_view_t *view) {
    if (vibe_snapshot(VIBE_WINDOW_TODAY, &view->today) == ESP_OK) {
        view->has_today = view->today.has_lkg || view->today.today_complete;
        view->data_state = view->today.state;
    }
    if (vibe_snapshot(VIBE_WINDOW_SEVEN_DAYS, &view->seven_day) == ESP_OK) {
        view->has_seven_day = view->seven_day.has_lkg;
        view->seven_day_coverage = bit_count7(view->seven_day.valid_days_mask);
    }
}

static void record_failure(app_controller_view_t *view, app_reason_t reason,
                           esp_err_t system_error, const char *detail) {
    view->busy = false;
    view->reason = reason;
    view->last_system_error = system_error;
    view->last_error = vibe_last_error();
    view->last_http_status = vibe_last_http_status();
    if (view->has_today || view->today.has_lkg) view->data_state = VIBE_STALE;
    set_detail(view, detail);
}

static uint32_t retry_delay_seconds(void) {
    uint32_t base = 60U;
    const uint8_t shifts = s_controller.failure_count > 5U
                               ? 5U
                               : s_controller.failure_count;
    base <<= shifts;
    if (base > 1800U) base = 1800U;
    const int32_t span = (int32_t)(base / 5U);
    const uint32_t random = esp_random();
    const int32_t jitter = span == 0
                               ? 0
                               : (int32_t)(random % (uint32_t)(2 * span + 1)) -
                                     span;
    return (uint32_t)((int32_t)base + jitter);
}

static void schedule_retry(void) {
    uint32_t delay = retry_delay_seconds();
    if (vibe_last_http_status() == 429) {
        const uint32_t server_delay = vibe_last_retry_after_seconds();
        if (server_delay > delay) delay = server_delay;
    }
    s_controller.next_retry_us = monotonic_us() +
        (int64_t)delay * 1000000LL;
    /* A past-due periodic deadline must not bypass this backoff. */
    s_controller.next_refresh_us = 0;
    if (s_controller.failure_count < UINT8_MAX) ++s_controller.failure_count;
}

static esp_err_t sync_system_time(void) {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t error = esp_netif_sntp_init(&config);
    if (error == ESP_OK) {
        error = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
        esp_netif_sntp_deinit();
    }
    if (system_time_valid()) {
        if (s_controller.config.time_synchronized_callback != NULL) {
            s_controller.config.time_synchronized_callback(
                (int64_t)time(NULL), s_controller.config.time_context);
        }
        return ESP_OK;
    }
    return error == ESP_OK ? ESP_ERR_TIMEOUT : error;
}

static esp_err_t apply_vibe_config(void) {
    vibe_config_t config = {
        .bootstrap_origin = s_controller.config.bootstrap_origin,
        .client_name = s_controller.config.client_name,
        .hostname = s_controller.config.hostname,
        .timezone = s_controller.config.settings.timezone,
        .generation = 1,
        .config_revision = s_controller.config.settings.config_revision,
        .refresh_seconds = s_controller.config.settings.refresh_seconds,
        .max_usage_body_bytes = 0,
        .max_usage_buckets = 0,
    };
    return vibe_init(&config);
}

static void start_device_link(void);
static void fetch_today(bool manual);

static void after_time_ready(void) {
    app_controller_view_t view;
    read_view(&view);
    update_clock_fields(&view);
    view.time_valid = true;
    view.has_auth = vibe_has_auth();
    view.last_system_error = ESP_OK;
    update_snapshots(&view);
    publish_view(&view);
    if (view.has_auth) {
        s_controller.next_refresh_us = monotonic_us();
    } else {
        s_controller.request_code_retry = true;
        s_controller.next_retry_us = monotonic_us();
    }
}

static void handle_time_sync(void) {
    app_controller_view_t view;
    read_view(&view);
    view.lifecycle = VIBE_APP_TIME_SYNC;
    view.busy = true;
    view.reason = APP_REASON_NONE;
    set_detail(&view, "Synchronizing time");
    publish_view(&view);

    const esp_err_t error = sync_system_time();
    if (error != ESP_OK) {
        read_view(&view);
        view.lifecycle = VIBE_APP_TIME_REQUIRED;
        record_failure(&view, APP_REASON_TIME, error, "Time sync failed");
        publish_view(&view);
        schedule_retry();
        return;
    }
    after_time_ready();
}

static void start_device_link(void) {
    if (!s_controller.online || !system_time_valid()) return;
    app_controller_view_t view;
    read_view(&view);
    clear_public_link(&view);
    view.lifecycle = VIBE_APP_DEVICE_LINK;
    view.data_state = VIBE_LINK_REQUIRED;
    view.busy = true;
    view.reason = APP_REASON_NONE;
    set_detail(&view, "Requesting secure link code");
    publish_view(&view);

    const esp_err_t error =
        vibe_request_device_code(&s_controller.device_code);
    read_view(&view);
    view.busy = false;
    view.last_error = vibe_last_error();
    view.last_http_status = vibe_last_http_status();
    if (error != ESP_OK) {
        record_failure(&view, APP_REASON_AUTH, error,
                       "Could not request link code; retrying");
        view.lifecycle = VIBE_APP_DEVICE_LINK;
        view.data_state = VIBE_LINK_REQUIRED;
        s_controller.request_code_retry = true;
        schedule_retry();
        publish_view(&view);
        return;
    }

    s_controller.link_interval_seconds = s_controller.device_code.interval;
    s_controller.next_link_poll_us = monotonic_us() +
        (int64_t)s_controller.device_code.interval * 1000000LL;
    copy_text(view.user_code, sizeof(view.user_code),
              s_controller.device_code.user_code);
    /* Version 15 / medium ECC (the NOTE4 renderer's ceiling) carries at most
     * 412 binary bytes. Keep headroom for library-mode differences. */
    const char *complete_uri =
        s_controller.device_code.verification_uri_complete;
    const char *uri = complete_uri[0] != '\0' && strlen(complete_uri) <= 400U
                          ? complete_uri
                          : s_controller.device_code.verification_uri;
    copy_text(view.verification_uri, sizeof(view.verification_uri), uri);
    view.link_deadline_monotonic =
        s_controller.device_code.monotonic_deadline_seconds;
    view.reason = APP_REASON_NONE;
    view.last_system_error = ESP_OK;
    set_detail(&view, "Scan QR and confirm the matching code");
    s_controller.request_code_retry = false;
    s_controller.failure_count = 0;
    s_controller.next_retry_us = 0;
    publish_view(&view);
}

static void poll_device_link(void) {
    if (s_controller.device_code.device_code[0] == '\0' ||
        !s_controller.online) {
        return;
    }
    const int64_t now_seconds = monotonic_us() / 1000000LL;
    app_controller_view_t view;
    read_view(&view);
    if (now_seconds >= s_controller.device_code.monotonic_deadline_seconds) {
        clear_public_link(&view);
        view.lifecycle = VIBE_APP_DEVICE_LINK;
        view.data_state = VIBE_LINK_REQUIRED;
        view.reason = APP_REASON_AUTH;
        set_detail(&view, "Link code expired; press OK to retry");
        publish_view(&view);
        return;
    }

    vibe_auth_result_t result;
    const esp_err_t error = vibe_poll_device_code(
        s_controller.device_code.device_code, &result);
    read_view(&view);
    view.last_error = vibe_last_error();
    view.last_http_status = vibe_last_http_status();
    if (error != ESP_OK) {
        record_failure(&view, APP_REASON_AUTH, error,
                       "Link check failed; waiting to retry");
        view.lifecycle = VIBE_APP_DEVICE_LINK;
        uint32_t wait = s_controller.link_interval_seconds < 5U
                            ? 5U
                            : s_controller.link_interval_seconds;
        if (view.last_http_status == 429 &&
            vibe_last_retry_after_seconds() > wait) {
            wait = vibe_last_retry_after_seconds();
        }
        const int64_t remaining =
            s_controller.device_code.monotonic_deadline_seconds -
            now_seconds;
        if (remaining > 0 && (int64_t)wait > remaining) {
            wait = (uint32_t)remaining;
        }
        s_controller.next_link_poll_us = monotonic_us() +
                                         (int64_t)wait * 1000000LL;
        publish_view(&view);
        return;
    }

    switch (result.status) {
        case VIBE_AUTH_PENDING:
            s_controller.next_link_poll_us = monotonic_us() +
                (int64_t)s_controller.link_interval_seconds * 1000000LL;
            break;
        case VIBE_AUTH_SLOW_DOWN:
            s_controller.link_interval_seconds +=
                result.retry_after_seconds < 5U ? 5U
                                                : result.retry_after_seconds;
            s_controller.next_link_poll_us = monotonic_us() +
                (int64_t)s_controller.link_interval_seconds * 1000000LL;
            set_detail(&view, "Server asked the device to slow down");
            publish_view(&view);
            break;
        case VIBE_AUTH_SUCCESS:
            clear_public_link(&view);
            view.has_auth = true;
            view.reason = APP_REASON_NONE;
            view.lifecycle = VIBE_APP_SYNCING;
            set_detail(&view, "Linked; loading usage");
            publish_view(&view);
            s_controller.next_refresh_us = monotonic_us();
            break;
        case VIBE_AUTH_DENIED:
            clear_public_link(&view);
            view.data_state = VIBE_LINK_REQUIRED;
            view.reason = APP_REASON_AUTH;
            set_detail(&view, "Link request denied; press OK to retry");
            publish_view(&view);
            break;
        case VIBE_AUTH_EXPIRED:
            clear_public_link(&view);
            view.data_state = VIBE_LINK_REQUIRED;
            view.reason = APP_REASON_AUTH;
            set_detail(&view, "Link code expired; press OK to retry");
            publish_view(&view);
            break;
        case VIBE_AUTH_ALREADY_DELIVERED:
            clear_public_link(&view);
            view.data_state = VIBE_LINK_REQUIRED;
            view.reason = APP_REASON_AUTH;
            set_detail(&view, "Code was already used; press OK for a new code");
            publish_view(&view);
            break;
        default:
            clear_public_link(&view);
            record_failure(&view, APP_REASON_AUTH, ESP_ERR_INVALID_RESPONSE,
                           "Unexpected link response");
            publish_view(&view);
            break;
    }
}

static void fetch_today(bool manual) {
    if (!s_controller.online || !system_time_valid() || !vibe_has_auth()) {
        return;
    }
    const int64_t now = monotonic_us();
    app_controller_view_t view;
    const uint32_t retry_remaining = vibe_retry_remaining_seconds();
    if (retry_remaining > 0U) {
        s_controller.next_retry_us =
            now + (int64_t)retry_remaining * 1000000LL;
        s_controller.next_refresh_us = 0;
        read_view(&view);
        view.lifecycle = VIBE_APP_DASHBOARD;
        view.busy = false;
        view.reason = APP_REASON_HTTP;
        if (view.has_today || view.today.has_lkg) {
            view.data_state = VIBE_STALE;
        }
        set_detail(&view, "Server retry window active; keeping saved data");
        publish_view(&view);
        return;
    }
    if (manual && s_controller.last_manual_refresh_us != 0 &&
        now - s_controller.last_manual_refresh_us < 60000000LL) {
        return;
    }
    if (manual) s_controller.last_manual_refresh_us = now;

    read_view(&view);
    view.lifecycle = VIBE_APP_SYNCING;
    view.busy = true;
    view.reason = APP_REASON_NONE;
    set_detail(&view, manual ? "Refreshing today" : "Syncing today");
    publish_view(&view);

    const esp_err_t error = vibe_fetch_today(&view.today);
    read_view(&view);
    view.busy = false;
    view.last_error = vibe_last_error();
    view.last_http_status = vibe_last_http_status();
    view.last_system_error = error;
    view.has_auth = vibe_has_auth();
    if (error != ESP_OK) {
        if (view.last_http_status == 401 && !view.has_auth) {
            clear_public_link(&view);
            view.has_today = false;
            view.has_seven_day = false;
            memset(&view.today, 0, sizeof(view.today));
            memset(&view.seven_day, 0, sizeof(view.seven_day));
            if (view.last_error == VIBE_ERR_STORAGE) {
                view.lifecycle = VIBE_APP_STORAGE_ERROR;
                view.data_state = VIBE_AUTH_REQUIRED;
                view.reason = APP_REASON_STORAGE;
                set_detail(&view,
                           "Access expired; unlink could not be saved");
            } else {
                view.lifecycle = VIBE_APP_DASHBOARD;
                view.data_state = VIBE_AUTH_REQUIRED;
                view.reason = APP_REASON_AUTH;
                set_detail(&view,
                           "Vibe access expired; reconnect required");
            }
        } else {
            view.lifecycle = VIBE_APP_DASHBOARD;
            record_failure(&view,
                           view.last_error == VIBE_ERR_STORAGE
                               ? APP_REASON_STORAGE
                               : APP_REASON_HTTP,
                           error, "Sync failed; showing last good data");
            schedule_retry();
        }
        publish_view(&view);
        return;
    }

    s_controller.failure_count = 0;
    s_controller.next_retry_us = 0;
    s_controller.next_refresh_us = monotonic_us() +
        (int64_t)s_controller.config.settings.refresh_seconds * 1000000LL;
    update_snapshots(&view);
    view.lifecycle = VIBE_APP_DASHBOARD;
    view.reason = APP_REASON_NONE;
    view.last_system_error = ESP_OK;
    view.data_state = view.today.state;
    set_detail(&view, view.today.state == VIBE_EMPTY
                          ? "Today has no usage"
                          : "Usage is up to date");
    if (!view.seven_day.seven_day_complete) {
        const esp_err_t reconcile_error = vibe_begin_reconcile(false);
        if (reconcile_error == ESP_OK) {
            s_controller.reconcile_active = true;
            s_controller.next_reconcile_us = monotonic_us() + 1000000LL;
        } else {
            view.lifecycle = VIBE_APP_STORAGE_ERROR;
            record_failure(&view, APP_REASON_STORAGE, reconcile_error,
                           "Today saved; could not queue 7D history");
        }
    }
    update_clock_fields(&view);
    publish_view(&view);
}

static void reconcile_one(void) {
    if (!s_controller.reconcile_active || !s_controller.online ||
        !system_time_valid() || !vibe_has_auth()) {
        return;
    }
    app_controller_view_t *view = &s_controller.scratch_view;
    const uint32_t retry_remaining = vibe_retry_remaining_seconds();
    if (retry_remaining > 0U) {
        s_controller.next_reconcile_us = monotonic_us() +
            (int64_t)retry_remaining * 1000000LL;
        read_view(view);
        view->lifecycle = VIBE_APP_DASHBOARD;
        view->busy = false;
        view->reason = APP_REASON_HTTP;
        if (view->has_today || view->today.has_lkg) {
            view->data_state = VIBE_STALE;
        }
        set_detail(view, "Server retry window active; 7D sync paused");
        publish_view(view);
        return;
    }
    read_view(view);
    view->lifecycle = VIBE_APP_SYNCING;
    view->busy = true;
    view->reason = APP_REASON_NONE;
    set_detail(view, "Filling seven-day history");
    publish_view(view);

    const esp_err_t error = vibe_reconcile(&view->seven_day);
    read_view(view);
    view->busy = false;
    view->last_error = vibe_last_error();
    view->last_http_status = vibe_last_http_status();
    if (error != ESP_OK) {
        s_controller.reconcile_active = false;
        view->lifecycle = VIBE_APP_DASHBOARD;
        record_failure(view,
                       view->last_error == VIBE_ERR_STORAGE
                           ? APP_REASON_STORAGE
                           : APP_REASON_HTTP,
                       error, "7D sync paused; completed days were kept");
        schedule_retry();
        publish_view(view);
        return;
    }
    update_snapshots(view);
    view->lifecycle = VIBE_APP_DASHBOARD;
    view->reason = APP_REASON_NONE;
    view->last_system_error = ESP_OK;
    if (!vibe_reconcile_pending()) {
        s_controller.reconcile_active = false;
        set_detail(view, view->seven_day.seven_day_complete
                              ? "Seven-day history is complete"
                              : "Seven-day history remains incomplete");
    } else {
        s_controller.next_reconcile_us = monotonic_us() + 1000000LL;
        /* Coverage is a separate typed field; keep display detail translatable. */
        set_detail(view, "Filling seven-day history");
    }
    publish_view(view);
}

static void handle_wifi_event(wifi_adapter_event_t event) {
    app_controller_view_t view;
    read_view(&view);
    view.wifi_has_credentials = wifi_adapter_has_credentials();
    view.wifi_connected = wifi_adapter_is_connected();
    view.wifi_provisioning = wifi_adapter_is_provisioning();
    switch (event) {
        case WIFI_ADAPTER_SCANNING:
            view.lifecycle = VIBE_APP_WIFI_CONNECTING;
            set_detail(&view, "Scanning saved Wi-Fi networks");
            publish_view(&view);
            break;
        case WIFI_ADAPTER_CONNECTING:
            view.lifecycle = VIBE_APP_WIFI_CONNECTING;
            set_detail(&view, "Connecting to Wi-Fi");
            publish_view(&view);
            break;
        case WIFI_ADAPTER_GOT_IP:
            s_controller.online = true;
            view.wifi_connected = true;
            view.wifi_provisioning = false;
            view.lifecycle = VIBE_APP_TIME_SYNC;
            view.reason = APP_REASON_NONE;
            set_detail(&view, "Wi-Fi connected");
            publish_view(&view);
            s_controller.time_sync_pending = true;
            break;
        case WIFI_ADAPTER_DISCONNECTED:
            s_controller.online = false;
            s_controller.time_sync_pending = false;
            vibe_cancel();
            view.wifi_connected = false;
            view.lifecycle = VIBE_APP_WIFI_CONNECTING;
            view.reason = APP_REASON_WIFI;
            if (view.has_today || view.today.has_lkg) {
                view.data_state = VIBE_STALE;
            }
            set_detail(&view, "Wi-Fi disconnected; retrying saved networks");
            publish_view(&view);
            break;
        case WIFI_ADAPTER_PORTAL_ENTER:
            s_controller.online = false;
            s_controller.time_sync_pending = false;
            vibe_cancel();
            view.wifi_provisioning = true;
            view.wifi_connected = false;
            view.lifecycle = VIBE_APP_WIFI_PROVISIONING;
            set_detail(&view, "Connect a phone to the setup hotspot");
            publish_view(&view);
            break;
        case WIFI_ADAPTER_PROVISIONING_TIMEOUT:
            // Timer callbacks only enqueue this event. The controller task
            // owns the blocking Wi-Fi teardown and its resulting exit event.
            wifi_adapter_stop_provisioning();
            view.wifi_provisioning = false;
            set_detail(&view, "Wi-Fi setup window expired");
            publish_view(&view);
            break;
        case WIFI_ADAPTER_PORTAL_EXIT:
            view.wifi_provisioning = false;
            view.wifi_has_credentials = wifi_adapter_has_credentials();
            view.lifecycle = view.wifi_has_credentials
                                 ? VIBE_APP_WIFI_CONNECTING
                                 : VIBE_APP_WIFI_REQUIRED;
            set_detail(&view, view.wifi_has_credentials
                                  ? "Setup closed; connecting to Wi-Fi"
                                  : "Wi-Fi setup closed");
            publish_view(&view);
            if (view.wifi_has_credentials) {
                (void)wifi_adapter_start_station();
            }
            break;
    }
}

static void restart_vibe_for_timezone(app_controller_view_t *view) {
    vibe_cancel();
    clear_public_link(view);
    ++s_controller.config.settings.config_revision;
    s_controller.config.settings.timezone =
        s_controller.config.settings.timezone == VIBE_TZ_ASIA_SHANGHAI
            ? VIBE_TZ_UTC
            : VIBE_TZ_ASIA_SHANGHAI;
    esp_err_t error = device_config_save(&s_controller.config.settings);
    if (error == ESP_OK) {
        setenv("TZ", vibe_timezone_posix(s_controller.config.settings.timezone),
               1);
        tzset();
        error = apply_vibe_config();
    }
    view->last_system_error = error;
    view->timezone = s_controller.config.settings.timezone;
    view->config_revision = s_controller.config.settings.config_revision;
    view->has_today = false;
    view->has_seven_day = false;
    memset(&view->today, 0, sizeof(view->today));
    memset(&view->seven_day, 0, sizeof(view->seven_day));
    if (error != ESP_OK) {
        view->lifecycle = VIBE_APP_STORAGE_ERROR;
        record_failure(view, APP_REASON_STORAGE, error,
                       "Could not save timezone");
        return;
    }
    view->has_auth = vibe_has_auth();
    set_detail(view, "Timezone changed; rebuilding daily cache");
    s_controller.reconcile_active = false;
    if (s_controller.online && system_time_valid()) {
        if (view->has_auth) {
            s_controller.next_refresh_us = monotonic_us();
        } else {
            s_controller.request_code_retry = true;
            s_controller.next_retry_us = monotonic_us();
        }
    }
}

static void handle_intent(app_intent_t intent) {
    app_controller_view_t view;
    read_view(&view);
    switch (intent) {
        case APP_INTENT_REFRESH:
            if (view.data_state == VIBE_LINK_REQUIRED ||
                view.data_state == VIBE_AUTH_REQUIRED || !vibe_has_auth()) {
                s_controller.request_code_retry = true;
                s_controller.next_retry_us = monotonic_us();
            } else {
                s_controller.manual_refresh_pending = true;
            }
            break;
        case APP_INTENT_RECONCILE:
            if (s_controller.online && system_time_valid() && vibe_has_auth()) {
                view.last_system_error = vibe_begin_reconcile(true);
                if (view.last_system_error == ESP_OK) {
                    s_controller.reconcile_active = true;
                    s_controller.next_reconcile_us = monotonic_us();
                    view.reason = APP_REASON_NONE;
                    set_detail(&view, "Seven-day reconciliation queued");
                } else {
                    view.lifecycle = VIBE_APP_STORAGE_ERROR;
                    record_failure(&view, APP_REASON_STORAGE,
                                   view.last_system_error,
                                   "Could not queue seven-day reconciliation");
                }
                publish_view(&view);
            }
            break;
        case APP_INTENT_START_WIFI:
        case APP_INTENT_RECONFIGURE_WIFI:
            vibe_cancel();
            s_controller.online = false;
            s_controller.time_sync_pending = false;
            wifi_adapter_stop_station();
            view.lifecycle = VIBE_APP_WIFI_PROVISIONING;
            view.wifi_connected = false;
            set_detail(&view, "Opening Wi-Fi setup for 10 minutes");
            publish_view(&view);
            view.last_system_error = wifi_adapter_start_provisioning();
            if (view.last_system_error != ESP_OK) {
                record_failure(&view, APP_REASON_WIFI, view.last_system_error,
                               "Could not start Wi-Fi setup");
                publish_view(&view);
            }
            break;
        case APP_INTENT_RELINK:
            view.last_system_error = vibe_unlink();
            clear_public_link(&view);
            view.has_auth = false;
            view.has_today = false;
            view.has_seven_day = false;
            memset(&view.today, 0, sizeof(view.today));
            memset(&view.seven_day, 0, sizeof(view.seven_day));
            view.data_state = VIBE_LINK_REQUIRED;
            view.reason = view.last_system_error == ESP_OK
                              ? APP_REASON_NONE
                              : APP_REASON_STORAGE;
            view.lifecycle = view.last_system_error == ESP_OK
                                 ? VIBE_APP_DEVICE_LINK
                                 : VIBE_APP_STORAGE_ERROR;
            set_detail(&view, view.last_system_error == ESP_OK
                                  ? "Requesting a new Vibe link"
                                  : "Could not persist account unlink");
            publish_view(&view);
            if (view.last_system_error == ESP_OK) {
                s_controller.request_code_retry = true;
                s_controller.next_retry_us = monotonic_us();
            }
            break;
        case APP_INTENT_UNLINK:
            view.last_system_error = vibe_unlink();
            clear_public_link(&view);
            view.has_auth = false;
            view.has_today = false;
            view.has_seven_day = false;
            memset(&view.today, 0, sizeof(view.today));
            memset(&view.seven_day, 0, sizeof(view.seven_day));
            view.data_state = VIBE_LINK_REQUIRED;
            view.lifecycle = VIBE_APP_DEVICE_LINK;
            view.reason = view.last_system_error == ESP_OK
                              ? APP_REASON_NONE
                              : APP_REASON_STORAGE;
            set_detail(&view, view.last_system_error == ESP_OK
                                  ? "Vibe account unlinked"
                                  : "Could not persist unlink");
            publish_view(&view);
            break;
        case APP_INTENT_TOGGLE_TIMEZONE:
            restart_vibe_for_timezone(&view);
            publish_view(&view);
            break;
        case APP_INTENT_CYCLE_LANGUAGE: {
            const vibe_language_t next = vibe_language_next(view.language);
            const esp_err_t error = device_config_save_language(next);
            if (error == ESP_OK) {
                s_controller.config.settings.language = next;
                view.language = next;
                wifi_adapter_set_language(vibe_language_code(next));
                set_detail(&view, "Language saved");
            } else {
                record_failure(&view, APP_REASON_STORAGE, error, "Could not save language");
            }
            publish_view(&view);
            break;
        }
        case APP_INTENT_FACTORY_RESET:
            view.lifecycle = VIBE_APP_RESETTING;
            view.busy = true;
            set_detail(&view, "Resetting local Vibe and Wi-Fi settings");
            publish_view(&view);
            vibe_cancel();
            wifi_adapter_stop_provisioning();
            wifi_adapter_stop_station();
            view.last_system_error = vibe_factory_reset();
            if (view.last_system_error == ESP_OK) {
                view.last_system_error = wifi_adapter_clear_credentials();
            }
            if (view.last_system_error == ESP_OK) {
                view.last_system_error = vibe_factory_reset_complete();
            }
            if (view.last_system_error == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(150));
                esp_restart();
            }
            view.busy = false;
            view.lifecycle = VIBE_APP_STORAGE_ERROR;
            record_failure(&view, APP_REASON_STORAGE, view.last_system_error,
                           "Reset did not complete");
            publish_view(&view);
            break;
    }
}

static void handle_boot(void) {
    app_controller_view_t view;
    read_view(&view);
    view.has_auth = vibe_has_auth();
    view.wifi_has_credentials = wifi_adapter_has_credentials();
    update_clock_fields(&view);
    if (view.time_valid) update_snapshots(&view);
    if (view.wifi_has_credentials) {
        view.lifecycle = VIBE_APP_WIFI_CONNECTING;
        set_detail(&view, "Connecting to saved Wi-Fi");
        publish_view(&view);
        const esp_err_t error = wifi_adapter_start_station();
        if (error != ESP_OK) {
            read_view(&view);
            record_failure(&view, APP_REASON_WIFI, error,
                           "Could not start Wi-Fi station");
            publish_view(&view);
        }
    } else {
        view.lifecycle = VIBE_APP_WIFI_PROVISIONING;
        view.wifi_provisioning = true;
        set_detail(&view, "Connect a phone to the setup hotspot");
        publish_view(&view);
        const esp_err_t error = wifi_adapter_start_provisioning();
        if (error != ESP_OK) {
            read_view(&view);
            view.lifecycle = VIBE_APP_WIFI_REQUIRED;
            record_failure(&view, APP_REASON_WIFI, error,
                           "Press OK to retry Wi-Fi setup");
            publish_view(&view);
        }
    }
}

static void handle_shutdown(void) {
    app_controller_view_t view;
    read_view(&view);
    view.lifecycle = VIBE_APP_PREPARE_SLEEP;
    view.busy = true;
    set_detail(&view, "Stopping network before power off");
    publish_view(&view);
    vibe_cancel();
    s_controller.reconcile_active = false;
    s_controller.time_sync_pending = false;
    clear_public_link(&view);
    wifi_adapter_stop_provisioning();
    wifi_adapter_stop_station();
    s_controller.online = false;
    view.busy = false;
    view.wifi_connected = false;
    view.wifi_provisioning = false;
    set_detail(&view, "Network stopped");
    publish_view(&view);
    xEventGroupSetBits(s_controller.events, APP_SHUTDOWN_READY_BIT);
}

static void run_due_actions(void) {
    const int64_t now = monotonic_us();
    if (!s_controller.online) return;
    if (s_controller.time_sync_pending) {
        s_controller.time_sync_pending = false;
        handle_time_sync();
        return;
    }
    if (s_controller.device_code.device_code[0] != '\0' &&
        s_controller.next_link_poll_us > 0 &&
        now >= s_controller.next_link_poll_us) {
        poll_device_link();
        return;
    }
    if (s_controller.manual_refresh_pending && vibe_has_auth()) {
        s_controller.manual_refresh_pending = false;
        fetch_today(true);
        return;
    }
    if (!s_controller.reconcile_active && s_controller.next_retry_us == 0 &&
        vibe_has_auth() && system_time_valid()) {
        app_controller_view_t *view = &s_controller.scratch_view;
        read_view(view);
        if (vibe_reconcile_pending() || daily_reconciliation_due(view)) {
            esp_err_t error = ESP_OK;
            if (!vibe_reconcile_pending()) {
                error = vibe_begin_reconcile(false);
            }
            if (error == ESP_OK) {
                s_controller.reconcile_active = true;
                s_controller.next_reconcile_us = now;
                set_detail(view, "Daily seven-day reconciliation queued");
            } else {
                view->lifecycle = VIBE_APP_STORAGE_ERROR;
                record_failure(view, APP_REASON_STORAGE, error,
                               "Could not queue daily reconciliation");
                schedule_retry();
            }
            publish_view(view);
            return;
        }
    }
    if (s_controller.reconcile_active && s_controller.next_reconcile_us > 0 &&
        now >= s_controller.next_reconcile_us) {
        reconcile_one();
        return;
    }
    if (vibe_has_auth() && s_controller.next_refresh_us > 0 &&
        now >= s_controller.next_refresh_us) {
        fetch_today(false);
        return;
    }
    if (s_controller.next_retry_us > 0 && now >= s_controller.next_retry_us) {
        s_controller.next_retry_us = 0;
        if (!system_time_valid()) {
            handle_time_sync();
        } else if (!vibe_has_auth() || s_controller.request_code_retry) {
            start_device_link();
        } else {
            fetch_today(false);
        }
    }
}

static void refresh_clock_view(void) {
    app_controller_view_t view;
    read_view(&view);
    const bool old_time_valid = view.time_valid;
    int64_t old_link_minutes = -1;
    if (view.lifecycle == VIBE_APP_DEVICE_LINK &&
        view.link_deadline_monotonic > 0) {
        int64_t remaining =
            view.link_deadline_monotonic - view.link_now_monotonic;
        if (remaining < 0) remaining = 0;
        old_link_minutes = (remaining + 59) / 60;
    }
    update_clock_fields(&view);
    int64_t new_link_minutes = -1;
    if (view.lifecycle == VIBE_APP_DEVICE_LINK &&
        view.link_deadline_monotonic > 0) {
        int64_t remaining =
            view.link_deadline_monotonic - view.link_now_monotonic;
        if (remaining < 0) remaining = 0;
        new_link_minutes = (remaining + 59) / 60;
    }
    if (view.time_valid != old_time_valid ||
        new_link_minutes != old_link_minutes) {
        publish_view(&view);
    }
}

static void controller_task(void *context) {
    (void)context;
    command_t command;
    while (true) {
        if (xQueueReceive(s_controller.queue, &command,
                          pdMS_TO_TICKS(250)) == pdTRUE) {
            switch (command.type) {
                case COMMAND_BOOT:
                    handle_boot();
                    break;
                case COMMAND_WIFI_EVENT:
                    handle_wifi_event(command.value.wifi_event);
                    break;
                case COMMAND_INTENT:
                    handle_intent(command.value.intent);
                    break;
                case COMMAND_SHUTDOWN:
                    handle_shutdown();
                    break;
            }
        }
        run_due_actions();
        refresh_clock_view();
    }
}

static void wifi_event_callback(wifi_adapter_event_t event, void *context) {
    (void)context;
    if (s_controller.queue == NULL) return;
    if (event == WIFI_ADAPTER_DISCONNECTED ||
        event == WIFI_ADAPTER_PORTAL_ENTER) {
        vibe_cancel();
    }
    const command_t command = {
        .type = COMMAND_WIFI_EVENT,
        .value.wifi_event = event,
    };
    (void)xQueueSend(s_controller.queue, &command, 0);
}

esp_err_t app_controller_init(const app_controller_config_t *config) {
    if (config == NULL || config->bootstrap_origin == NULL ||
        config->client_name == NULL || config->product_prefix == NULL ||
        config->hostname == NULL || config->settings.device_id[0] == '\0' ||
        s_controller.initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_controller, 0, sizeof(s_controller));
    s_controller.config = *config;
    if (!copy_text(s_controller.bootstrap_origin,
                   sizeof(s_controller.bootstrap_origin),
                   config->bootstrap_origin) ||
        !copy_text(s_controller.client_name, sizeof(s_controller.client_name),
                   config->client_name) ||
        !copy_text(s_controller.product_prefix,
                   sizeof(s_controller.product_prefix),
                   config->product_prefix) ||
        !copy_text(s_controller.hostname, sizeof(s_controller.hostname),
                   config->hostname)) {
        return ESP_ERR_INVALID_SIZE;
    }
    s_controller.config.bootstrap_origin = s_controller.bootstrap_origin;
    s_controller.config.client_name = s_controller.client_name;
    s_controller.config.product_prefix = s_controller.product_prefix;
    s_controller.config.hostname = s_controller.hostname;
    s_controller.queue = xQueueCreate(APP_QUEUE_DEPTH, sizeof(command_t));
    s_controller.view_mutex = xSemaphoreCreateMutex();
    s_controller.events = xEventGroupCreate();
    if (s_controller.queue == NULL || s_controller.view_mutex == NULL ||
        s_controller.events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    bool reset_pending = false;
    esp_err_t error = vibe_factory_reset_pending(&reset_pending);
    if (error != ESP_OK) return error;
    if (reset_pending) {
        /*
         * Re-enter the idempotent local half before any credential can be used.
         * The journal remains set until Wi-Fi is cleared below.
         */
        error = vibe_factory_reset_resume_local();
        if (error != ESP_OK) return error;
    }

    setenv("TZ", vibe_timezone_posix(config->settings.timezone), 1);
    tzset();
    error = apply_vibe_config();
    if (error != ESP_OK) return error;

    wifi_adapter_config_t wifi_config = {
        .product_prefix = config->product_prefix,
        .device_id = config->settings.device_id,
        .station_hostname = config->hostname,
        .product_name = config->client_name,
        .firmware_version = esp_app_get_description()->version,
        .repository_url = VIBE_REPOSITORY_URL,
        .author_url = VIBE_AUTHOR_URL,
        .language = vibe_language_code(config->settings.language),
        .provisioning_timeout_seconds = 600,
        .event_callback = wifi_event_callback,
        .event_context = NULL,
    };
    error = wifi_adapter_init(&wifi_config);
    if (error != ESP_OK) return error;
    if (reset_pending) {
        error = wifi_adapter_clear_credentials();
        if (error == ESP_OK) error = vibe_factory_reset_complete();
        if (error != ESP_OK) return error;
        ESP_LOGW(TAG, "completed interrupted factory reset; restarting");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }

    app_controller_view_t *view = &s_controller.view;
    memset(view, 0, sizeof(*view));
    view->lifecycle = VIBE_APP_LOAD_CONFIG;
    view->data_state = vibe_has_auth() ? VIBE_STALE : VIBE_LINK_REQUIRED;
    view->has_auth = vibe_has_auth();
    view->wifi_has_credentials = wifi_adapter_has_credentials();
    view->generation = vibe_account_generation();
    view->config_revision = config->settings.config_revision;
    view->refresh_seconds = config->settings.refresh_seconds;
    view->timezone = config->settings.timezone;
    view->language = config->settings.language;
    copy_text(view->device_id, sizeof(view->device_id),
              config->settings.device_id);
    copy_text(view->ap_ssid, sizeof(view->ap_ssid), wifi_adapter_ap_ssid());
    copy_text(view->portal_url, sizeof(view->portal_url),
              wifi_adapter_portal_url());
    set_detail(view, "Loading local state");
    s_controller.initialized = true;
    return ESP_OK;
}

esp_err_t app_controller_start(void) {
    if (!s_controller.initialized || s_controller.started) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t stack = s_controller.config.task_stack_bytes == 0
                               ? APP_TASK_STACK_DEFAULT
                               : s_controller.config.task_stack_bytes;
    const UBaseType_t priority = s_controller.config.task_priority == 0
                                    ? APP_TASK_PRIORITY_DEFAULT
                                    : s_controller.config.task_priority;
    if (xTaskCreate(controller_task, "vibe_controller", stack, NULL, priority,
                    &s_controller.task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_controller.started = true;
    const command_t command = {.type = COMMAND_BOOT};
    return xQueueSend(s_controller.queue, &command, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

esp_err_t app_controller_dispatch(app_intent_t intent) {
    if (!s_controller.started || intent > APP_INTENT_FACTORY_RESET) {
        return ESP_ERR_INVALID_STATE;
    }
    if (intent == APP_INTENT_START_WIFI ||
        intent == APP_INTENT_RECONFIGURE_WIFI ||
        intent == APP_INTENT_RELINK || intent == APP_INTENT_UNLINK ||
        intent == APP_INTENT_TOGGLE_TIMEZONE ||
        intent == APP_INTENT_FACTORY_RESET) {
        vibe_cancel();
    }
    const command_t command = {
        .type = COMMAND_INTENT,
        .value.intent = intent,
    };
    return xQueueSend(s_controller.queue, &command, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

esp_err_t app_controller_get_view(app_controller_view_t *view) {
    if (!s_controller.initialized || view == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_controller.view_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *view = s_controller.view;
    xSemaphoreGive(s_controller.view_mutex);
    return ESP_OK;
}

esp_err_t app_controller_prepare_shutdown(uint32_t timeout_ms) {
    if (!s_controller.started) return ESP_ERR_INVALID_STATE;
    vibe_cancel();
    xEventGroupClearBits(s_controller.events, APP_SHUTDOWN_READY_BIT);
    const command_t command = {.type = COMMAND_SHUTDOWN};
    if (xQueueSend(s_controller.queue, &command, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const EventBits_t bits = xEventGroupWaitBits(
        s_controller.events, APP_SHUTDOWN_READY_BIT, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & APP_SHUTDOWN_READY_BIT) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}
