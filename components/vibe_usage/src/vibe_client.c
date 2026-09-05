#include "vibe_client.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "vibe_date.h"
#include "vibe_device_flow_policy.h"
#include "vibe_internal.h"
#include "vibe_json_safety.h"
#include "vibe_parser.h"

vibe_context_t g_vibe;

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t length;
} body_buffer_t;

static void set_error(vibe_error_t error, int http_status) {
    g_vibe.last_error = error;
    g_vibe.last_http_status = http_status;
}

static void mark_cache_dirty(void) {
    if (g_vibe.cache.persisted) {
        ++g_vibe.cache.sequence;
        g_vibe.cache.persisted = false;
    }
}

static size_t bounded_strlen(const char *text, size_t limit) {
    size_t length = 0;
    if (text == NULL) return limit;
    while (length < limit && text[length] != '\0') ++length;
    return length;
}

static void secure_wipe(void *data, size_t size) {
    volatile uint8_t *cursor = (volatile uint8_t *)data;
    while (cursor != NULL && size-- != 0) *cursor++ = 0;
}

static void wipe_json_string(cJSON *item) {
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        secure_wipe(item->valuestring, strlen(item->valuestring));
    }
}

static uint32_t next_account_generation(void) {
    const uint32_t next = g_vibe.auth.generation + 1U;
    return next == 0U ? 1U : next;
}

static int64_t newest_cache_timestamp(const vibe_cache_t *cache) {
    int64_t newest = cache->last_reconcile_at;
    for (size_t index = 0; index < VIBE_CACHE_DAYS; ++index) {
        if (cache->days[index].valid && cache->days[index].fetched_at > newest) {
            newest = cache->days[index].fetched_at;
        }
    }
    return newest;
}

static esp_err_t write_cache_now(int64_t now_utc) {
    const esp_err_t error = vibe_store_write_cache(&g_vibe.cache);
    if (error == ESP_OK) g_vibe.last_cache_write_at = now_utc;
    return error;
}

static bool copy_string(char *output, size_t output_size, const char *input) {
    if (output == NULL || input == NULL || output_size == 0) return false;
    const size_t length = bounded_strlen(input, output_size);
    if (length == 0 || length >= output_size) return false;
    memcpy(output, input, length + 1U);
    return true;
}

static bool normalize_origin(const char *input, char output[VIBE_ORIGIN_BYTES]) {
    if (input == NULL) return false;
    size_t length = bounded_strlen(input, VIBE_ORIGIN_BYTES);
    if (length >= VIBE_ORIGIN_BYTES) return false;
    if (length > 0 && input[length - 1U] == '/') --length;
    if (length < 9 || length >= VIBE_ORIGIN_BYTES ||
        strncmp(input, "https://", 8) != 0) {
        return false;
    }
    for (size_t i = 8; i < length; ++i) {
        const char c = input[i];
        if (c == '/' || c == '?' || c == '#' || c == '@' || c <= ' ') {
            return false;
        }
    }
    memcpy(output, input, length);
    output[length] = '\0';
    return true;
}

static cJSON *unique_item(cJSON *object, const char *name) {
    cJSON *result = NULL;
    unsigned count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, object) {
        if (item->string != NULL && strcmp(item->string, name) == 0) {
            result = item;
            ++count;
        }
    }
    return count == 1U ? result : NULL;
}

static unsigned item_count(cJSON *object, const char *name) {
    unsigned count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, object) {
        if (item->string != NULL && strcmp(item->string, name) == 0) ++count;
    }
    return count;
}

static bool positive_u32(cJSON *item, uint32_t *value) {
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0 ||
        item->valuedouble > (double)UINT32_MAX) {
        return false;
    }
    const uint32_t converted = (uint32_t)item->valuedouble;
    if ((double)converted != item->valuedouble) return false;
    *value = converted;
    return true;
}

static vibe_error_t collect_body(const uint8_t *data, size_t size,
                                 void *context) {
    body_buffer_t *buffer = (body_buffer_t *)context;
    if (buffer == NULL || buffer->length > buffer->capacity ||
        size > buffer->capacity - buffer->length) {
        return VIBE_ERR_BODY_TOO_LARGE;
    }
    memcpy(buffer->data + buffer->length, data, size);
    buffer->length += size;
    return VIBE_OK;
}

static esp_err_t post_json(const char *path, const char *body,
                           body_buffer_t *response, int *status) {
    char url[VIBE_VERIFICATION_URL_BYTES];
    const int length = snprintf(url, sizeof(url), "%s%s",
                                g_vibe.bootstrap_origin, path);
    if (length <= 0 || (size_t)length >= sizeof(url)) return ESP_ERR_INVALID_SIZE;
    size_t received = 0;
    vibe_error_t body_error = VIBE_OK;
    uint32_t retry_after_seconds = 0;
    esp_err_t error = vibe_http_request(
        "POST", url, NULL, body, response->capacity, collect_body, response,
        15000, status, &received, &body_error, &retry_after_seconds);
    g_vibe.last_retry_after_seconds = retry_after_seconds;
    /* Both callers reserve one sentinel byte beyond capacity. */
    response->data[response->length] = '\0';
    if (body_error != VIBE_OK) set_error(body_error, *status);
    return error;
}

static cJSON *make_string_body(const char *key, const char *value,
                               const char *second_key,
                               const char *second_value) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;
    if (!cJSON_AddStringToObject(root, key, value) ||
        (second_key != NULL &&
         !cJSON_AddStringToObject(root, second_key, second_value))) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static esp_err_t current_day(int32_t *date_key) {
    const time_t now = time(NULL);
    if (date_key == NULL) {
        set_error(VIBE_ERR_INVALID_ARGUMENT, 0);
        return ESP_ERR_INVALID_ARG;
    }
    if (now < 1704067200 ||
        vibe_date_from_epoch((int64_t)now, g_vibe.timezone, date_key) !=
            VIBE_OK) {
        set_error(VIBE_ERR_TIME_INVALID, 0);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t vibe_init(const vibe_config_t *config) {
    if (config == NULL || config->client_name == NULL ||
        config->hostname == NULL ||
        !normalize_origin(config->bootstrap_origin,
                          g_vibe.bootstrap_origin) ||
        !copy_string(g_vibe.client_name, sizeof(g_vibe.client_name),
                     config->client_name) ||
        !copy_string(g_vibe.hostname, sizeof(g_vibe.hostname),
                     config->hostname) ||
        (config->timezone != VIBE_TZ_ASIA_SHANGHAI &&
         config->timezone != VIBE_TZ_UTC)) {
        memset(&g_vibe, 0, sizeof(g_vibe));
        g_vibe.last_error = VIBE_ERR_INVALID_ARGUMENT;
        return ESP_ERR_INVALID_ARG;
    }
    g_vibe.timezone = config->timezone;
    g_vibe.config_revision = config->config_revision == 0
                                 ? 1U
                                 : config->config_revision;
    g_vibe.refresh_seconds = config->refresh_seconds == 0
                                 ? 900U
                                 : config->refresh_seconds;
    g_vibe.max_usage_body_bytes = config->max_usage_body_bytes == 0
                                      ? VIBE_PARSER_DEFAULT_MAX_BODY
                                      : config->max_usage_body_bytes;
    g_vibe.max_usage_buckets = config->max_usage_buckets == 0
                                   ? VIBE_PARSER_DEFAULT_MAX_BUCKETS
                                   : config->max_usage_buckets;
    atomic_store(&g_vibe.cancel_generation, 1U);
    g_vibe.last_error = VIBE_OK;

    esp_err_t retry_error =
        vibe_store_load_retry_not_before(&g_vibe.retry_not_before_utc);
    if (retry_error != ESP_OK) {
        g_vibe.retry_not_before_utc = 0;
        g_vibe.last_error = VIBE_ERR_STORAGE;
    }

    esp_err_t auth_error = vibe_store_load_auth(&g_vibe.auth);
    if (auth_error != ESP_OK) {
        memset(&g_vibe.auth, 0, sizeof(g_vibe.auth));
        g_vibe.auth.present = true;
        g_vibe.auth.tombstone = true;
        g_vibe.auth.generation = config->generation == 0 ? 1U
                                                         : config->generation;
        if (auth_error != ESP_ERR_NVS_NOT_FOUND) g_vibe.last_error = VIBE_ERR_STORAGE;
    }
    if (g_vibe.auth.generation == 0) g_vibe.auth.generation = 1;

    vibe_cache_init(&g_vibe.cache, g_vibe.auth.generation,
                    g_vibe.config_revision, g_vibe.timezone);
    if (!g_vibe.auth.tombstone) {
        esp_err_t cache_error = vibe_store_load_cache(
            &g_vibe.cache, &g_vibe.auth, g_vibe.config_revision,
            g_vibe.timezone);
        if (cache_error != ESP_OK) {
            vibe_cache_init(&g_vibe.cache, g_vibe.auth.generation,
                            g_vibe.config_revision, g_vibe.timezone);
        }
    }
    g_vibe.last_cache_write_at = newest_cache_timestamp(&g_vibe.cache);
    g_vibe.initialized = true;
    ESP_LOGI("vibe_client", "local state loaded: auth=%u cache_restored=%u error=%s",
             (unsigned)vibe_has_auth(), (unsigned)g_vibe.cache.persisted,
             vibe_error_name(g_vibe.last_error));
    return ESP_OK;
}

esp_err_t vibe_request_device_code(vibe_device_code_t *result) {
    if (!g_vibe.initialized || result == NULL) return ESP_ERR_INVALID_STATE;
    g_vibe.last_retry_after_seconds = 0;
    memset(result, 0, sizeof(*result));
    const uint32_t operation = atomic_load(&g_vibe.cancel_generation);
    cJSON *request = make_string_body("clientName", g_vibe.client_name,
                                      "hostname", g_vibe.hostname);
    if (request == NULL) {
        set_error(VIBE_ERR_NO_MEMORY, 0);
        return ESP_ERR_NO_MEM;
    }
    char *request_text = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (request_text == NULL) {
        set_error(VIBE_ERR_NO_MEMORY, 0);
        return ESP_ERR_NO_MEM;
    }

    uint8_t *storage = (uint8_t *)malloc(8193U);
    if (storage == NULL) {
        free(request_text);
        set_error(VIBE_ERR_NO_MEMORY, 0);
        return ESP_ERR_NO_MEM;
    }
    body_buffer_t response = {.data = storage, .capacity = 8192U, .length = 0};
    int status = 0;
    esp_err_t error = post_json("/api/usage/device/code", request_text,
                                &response, &status);
    memset(request_text, 0, strlen(request_text));
    free(request_text);
    g_vibe.last_http_status = status;
    const bool cancelled =
        operation != atomic_load(&g_vibe.cancel_generation);
    if (error != ESP_OK || status != 200 || cancelled) {
        memset(storage, 0, 8193U);
        free(storage);
        set_error(cancelled ? VIBE_ERR_CANCELLED
                            : (error == ESP_OK ? VIBE_ERR_HTTP_STATUS
                                               : g_vibe.last_error),
                  status);
        if (cancelled) return ESP_ERR_INVALID_STATE;
        return error != ESP_OK ? error : ESP_ERR_INVALID_RESPONSE;
    }
    if (!vibe_json_text_is_cstring_safe(storage, response.length)) {
        secure_wipe(storage, 8193U);
        free(storage);
        set_error(VIBE_ERR_SCHEMA, status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(
        (const char *)storage, response.length + 1U, &parse_end, true);
    const bool root_is_object =
        cJSON_IsObject(root) &&
        parse_end == (const char *)storage + response.length;
    cJSON *device = root_is_object ? unique_item(root, "deviceCode") : NULL;
    cJSON *user = root ? unique_item(root, "userCode") : NULL;
    cJSON *uri = root ? unique_item(root, "verificationUri") : NULL;
    cJSON *complete = root ? unique_item(root, "verificationUriComplete") : NULL;
    cJSON *expires = root ? unique_item(root, "expiresIn") : NULL;
    cJSON *interval = root ? unique_item(root, "interval") : NULL;
    const bool valid = root_is_object && cJSON_IsString(device) &&
        cJSON_IsString(user) &&
        cJSON_IsString(uri) && cJSON_IsString(complete) &&
        copy_string(result->device_code, sizeof(result->device_code),
                    device->valuestring) &&
        copy_string(result->user_code, sizeof(result->user_code),
                    user->valuestring) &&
        copy_string(result->verification_uri, sizeof(result->verification_uri),
                    uri->valuestring) &&
        copy_string(result->verification_uri_complete,
                    sizeof(result->verification_uri_complete),
                    complete->valuestring) &&
        vibe_device_flow_url_is_trusted(result->verification_uri,
                                        g_vibe.bootstrap_origin) &&
        vibe_device_flow_url_is_trusted(result->verification_uri_complete,
                                        g_vibe.bootstrap_origin) &&
        positive_u32(expires, &result->expires_in) &&
        positive_u32(interval, &result->interval);
    wipe_json_string(device);
    wipe_json_string(user);
    wipe_json_string(uri);
    wipe_json_string(complete);
    cJSON_Delete(root);
    memset(storage, 0, 8193U);
    free(storage);
    if (!valid) {
        memset(result, 0, sizeof(*result));
        set_error(VIBE_ERR_SCHEMA, status);
        return ESP_ERR_INVALID_RESPONSE;
    }
    result->monotonic_deadline_seconds =
        esp_timer_get_time() / 1000000LL + result->expires_in;
    set_error(VIBE_OK, status);
    return ESP_OK;
}

esp_err_t vibe_poll_device_code(const char *device_code,
                                vibe_auth_result_t *result) {
    if (!g_vibe.initialized || result == NULL || device_code == NULL ||
        device_code[0] == '\0' ||
        bounded_strlen(device_code, VIBE_DEVICE_CODE_BYTES) >=
            VIBE_DEVICE_CODE_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    g_vibe.last_retry_after_seconds = 0;
    memset(result, 0, sizeof(*result));
    const uint32_t operation = atomic_load(&g_vibe.cancel_generation);
    cJSON *request = make_string_body("deviceCode", device_code, NULL, NULL);
    if (request == NULL) {
        set_error(VIBE_ERR_NO_MEMORY, 0);
        return ESP_ERR_NO_MEM;
    }
    char *request_text = cJSON_PrintUnformatted(request);
    wipe_json_string(unique_item(request, "deviceCode"));
    cJSON_Delete(request);
    if (request_text == NULL) {
        set_error(VIBE_ERR_NO_MEMORY, 0);
        return ESP_ERR_NO_MEM;
    }
    uint8_t *storage = (uint8_t *)malloc(8193U);
    if (storage == NULL) {
        memset(request_text, 0, strlen(request_text));
        free(request_text);
        set_error(VIBE_ERR_NO_MEMORY, 0);
        return ESP_ERR_NO_MEM;
    }
    body_buffer_t response = {.data = storage, .capacity = 8192U, .length = 0};
    int status = 0;
    esp_err_t error = post_json("/api/usage/device/poll", request_text,
                                &response, &status);
    memset(request_text, 0, strlen(request_text));
    free(request_text);
    g_vibe.last_http_status = status;
    const bool cancelled =
        operation != atomic_load(&g_vibe.cancel_generation);
    if (error != ESP_OK || (status != 200 && status != 410) || cancelled) {
        memset(storage, 0, 8193U);
        free(storage);
        set_error(cancelled ? VIBE_ERR_CANCELLED
                            : (error == ESP_OK ? VIBE_ERR_HTTP_STATUS
                                               : g_vibe.last_error),
                  status);
        if (cancelled) return ESP_ERR_INVALID_STATE;
        return error != ESP_OK ? error : ESP_ERR_INVALID_RESPONSE;
    }
    if (!vibe_json_text_is_cstring_safe(storage, response.length)) {
        secure_wipe(storage, 8193U);
        free(storage);
        set_error(VIBE_ERR_SCHEMA, status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(
        (const char *)storage, response.length + 1U, &parse_end, true);
    const bool root_is_object =
        cJSON_IsObject(root) &&
        parse_end == (const char *)storage + response.length;
    const unsigned key_count = root_is_object ? item_count(root, "apiKey") : 0;
    const unsigned origin_count = root_is_object ? item_count(root, "apiUrl") : 0;
    const unsigned error_count = root_is_object ? item_count(root, "error") : 0;
    cJSON *key = root_is_object ? unique_item(root, "apiKey") : NULL;
    cJSON *origin_item = root_is_object ? unique_item(root, "apiUrl") : NULL;
    cJSON *error_item = root_is_object ? unique_item(root, "error") : NULL;
    const bool key_is_valid =
        cJSON_IsString(key) && vibe_device_flow_api_key_is_valid(
                                   key->valuestring, VIBE_API_KEY_BYTES);
    const vibe_device_poll_shape_t shape =
        root_is_object
            ? vibe_device_poll_classify(
                  status, key_count, key_is_valid, cJSON_IsNull(key),
                  origin_count, cJSON_IsString(origin_item),
                  cJSON_IsNull(origin_item), error_count,
                  cJSON_IsString(error_item), cJSON_IsNull(error_item))
            : VIBE_DEVICE_POLL_SHAPE_INVALID;
    if (shape == VIBE_DEVICE_POLL_SHAPE_ALREADY_DELIVERED) {
        result->status = VIBE_AUTH_ALREADY_DELIVERED;
    } else if (shape == VIBE_DEVICE_POLL_SHAPE_SUCCESS) {
        char normalized[VIBE_ORIGIN_BYTES];
        const char *returned_origin = cJSON_IsString(origin_item)
                                          ? origin_item->valuestring
                                          : g_vibe.bootstrap_origin;
        if (!normalize_origin(returned_origin, normalized) ||
            strcmp(normalized, g_vibe.bootstrap_origin) != 0) {
            result->status = VIBE_AUTH_PROTOCOL_ERROR;
        } else {
            vibe_auth_record_t next = {
                .present = true,
                .tombstone = false,
                .generation = next_account_generation(),
            };
            copy_string(next.origin, sizeof(next.origin), normalized);
            copy_string(next.api_key, sizeof(next.api_key), key->valuestring);
            esp_err_t store_error = vibe_store_write_auth(&next);
            if (store_error == ESP_OK) {
                memset(&g_vibe.auth, 0, sizeof(g_vibe.auth));
                g_vibe.auth = next;
                // Generation isolation makes stale A/B slots unreadable even
                // when best-effort physical deletion is unavailable.
                (void)vibe_store_clear_cache();
                vibe_cache_init(&g_vibe.cache, next.generation,
                                g_vibe.config_revision, g_vibe.timezone);
                result->status = VIBE_AUTH_SUCCESS;
            } else {
                memset(&next, 0, sizeof(next));
                wipe_json_string(key);
                cJSON_Delete(root);
                memset(storage, 0, 8193U);
                free(storage);
                set_error(VIBE_ERR_STORAGE, status);
                return store_error;
            }
            memset(&next, 0, sizeof(next));
        }
    } else if (shape == VIBE_DEVICE_POLL_SHAPE_ERROR) {
        if (strcmp(error_item->valuestring, "authorization_pending") == 0) {
            result->status = VIBE_AUTH_PENDING;
        } else if (strcmp(error_item->valuestring, "access_denied") == 0) {
            result->status = VIBE_AUTH_DENIED;
        } else if (strcmp(error_item->valuestring, "expired_token") == 0) {
            result->status = VIBE_AUTH_EXPIRED;
        } else if (strcmp(error_item->valuestring, "slow_down") == 0) {
            result->status = VIBE_AUTH_SLOW_DOWN;
            result->retry_after_seconds = 5;
        } else {
            result->status = VIBE_AUTH_PROTOCOL_ERROR;
        }
    } else {
        result->status = VIBE_AUTH_PROTOCOL_ERROR;
    }
    wipe_json_string(key);
    cJSON_Delete(root);
    memset(storage, 0, 8193U);
    free(storage);
    set_error(result->status == VIBE_AUTH_PROTOCOL_ERROR ? VIBE_ERR_SCHEMA
                                                         : VIBE_OK,
              status);
    return result->status == VIBE_AUTH_PROTOCOL_ERROR ? ESP_ERR_INVALID_RESPONSE
                                                      : ESP_OK;
}

static vibe_error_t parser_callback(const uint8_t *data, size_t size,
                                    void *context) {
    return vibe_usage_parser_feed((vibe_usage_parser_t *)context, data, size);
}

static esp_err_t fetch_day(int32_t date_key) {
    if (!vibe_has_auth()) return ESP_ERR_INVALID_STATE;
    g_vibe.last_retry_after_seconds = 0;
    char from[25], to[25];
    if (vibe_date_api_bounds(date_key, g_vibe.timezone, from, to) != VIBE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[768];
    const int url_length = snprintf(
        url, sizeof(url), "%s/api/usage?from=%s&to=%s&tz=%s",
        g_vibe.auth.origin, from, to,
        g_vibe.timezone == VIBE_TZ_UTC ? "UTC" : "Asia%2FShanghai");
    if (url_length <= 0 || (size_t)url_length >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }
    char authorization[VIBE_API_KEY_BYTES + 8U];
    const int auth_length = snprintf(authorization, sizeof(authorization),
                                     "Bearer %s", g_vibe.auth.api_key);
    if (auth_length <= 0 || (size_t)auth_length >= sizeof(authorization)) {
        return ESP_ERR_INVALID_SIZE;
    }

    vibe_day_candidate_t *candidate =
        (vibe_day_candidate_t *)calloc(1, sizeof(*candidate));
    vibe_usage_parser_t *parser =
        (vibe_usage_parser_t *)calloc(1, sizeof(*parser));
    if (candidate == NULL || parser == NULL) {
        free(candidate);
        free(parser);
        memset(authorization, 0, sizeof(authorization));
        set_error(VIBE_ERR_NO_MEMORY, 0);
        return ESP_ERR_NO_MEM;
    }
    vibe_usage_parser_init(parser, candidate, date_key, g_vibe.timezone);
    vibe_usage_parser_set_limits(parser, g_vibe.max_usage_body_bytes,
                                 g_vibe.max_usage_buckets);
    const uint32_t operation = atomic_load(&g_vibe.cancel_generation);
    const uint32_t account_generation = g_vibe.auth.generation;
    size_t received = 0;
    int status = 0;
    vibe_error_t body_error = VIBE_OK;
    uint32_t retry_after_seconds = 0;
    esp_err_t http_error = vibe_http_request(
        "GET", url, authorization, NULL, g_vibe.max_usage_body_bytes,
        parser_callback, parser, 30000, &status, &received, &body_error,
        &retry_after_seconds);
    g_vibe.last_retry_after_seconds = retry_after_seconds;
    memset(authorization, 0, sizeof(authorization));
    g_vibe.last_http_status = status;
    if (status == 429 && retry_after_seconds > 0U) {
        const int64_t now_utc = (int64_t)time(NULL);
        const int64_t next_utc = now_utc + (int64_t)retry_after_seconds;
        if (now_utc >= 1704067200LL &&
            next_utc > g_vibe.retry_not_before_utc) {
            g_vibe.retry_not_before_utc = next_utc;
            (void)vibe_store_write_retry_not_before(next_utc);
        }
    }

    esp_err_t result = ESP_OK;
    if (operation != atomic_load(&g_vibe.cancel_generation) ||
        account_generation != g_vibe.auth.generation) {
        set_error(VIBE_ERR_CANCELLED, status);
        result = ESP_ERR_INVALID_STATE;
    } else if (status == 401) {
        const esp_err_t unlink_error = vibe_unlink();
        if (unlink_error == ESP_OK) {
            set_error(VIBE_ERR_HTTP_STATUS, status);
            result = ESP_ERR_INVALID_STATE;
        } else {
            set_error(VIBE_ERR_STORAGE, status);
            result = unlink_error;
        }
    } else if (http_error != ESP_OK || status != 200) {
        set_error(body_error != VIBE_OK ? body_error : VIBE_ERR_HTTP_STATUS,
                  status);
        result = http_error != ESP_OK ? http_error : ESP_ERR_INVALID_RESPONSE;
    } else {
        const vibe_error_t parse_error = vibe_usage_parser_finish(parser);
        if (parse_error != VIBE_OK) {
            set_error(parse_error, status);
            result = ESP_ERR_INVALID_RESPONSE;
        } else {
            const vibe_error_t cache_error = vibe_cache_replace_day(
                &g_vibe.cache, candidate, (int64_t)time(NULL));
            if (cache_error != VIBE_OK) {
                set_error(cache_error, status);
                result = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    if (result == ESP_OK && g_vibe.retry_not_before_utc != 0) {
        g_vibe.retry_not_before_utc = 0;
        (void)vibe_store_clear_retry_not_before();
    }
    memset(candidate, 0, sizeof(*candidate));
    memset(parser, 0, sizeof(*parser));
    free(candidate);
    free(parser);
    return result;
}

esp_err_t vibe_fetch_today(vibe_usage_snapshot_t *snapshot) {
    if (!g_vibe.initialized) return ESP_ERR_INVALID_STATE;
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    int32_t today;
    esp_err_t error = current_day(&today);
    if (error != ESP_OK) return error;
    error = fetch_day(today);
    if (error != ESP_OK) return error;
    bool progress_changed = false;
    if (g_vibe.cache.reconcile_anchor_key != today) {
        g_vibe.cache.reconcile_anchor_key = today;
        g_vibe.cache.reconcile_pending_mask = 0;
        progress_changed = true;
    } else if ((g_vibe.cache.reconcile_pending_mask & 1U) != 0) {
        g_vibe.cache.reconcile_pending_mask &= (uint8_t)~1U;
        progress_changed = true;
    }
    if (progress_changed) mark_cache_dirty();
    const int64_t now_utc = (int64_t)time(NULL);
    const bool checkpoint_due = g_vibe.last_cache_write_at <= 0 ||
        now_utc - g_vibe.last_cache_write_at >= 3600LL;
    esp_err_t store_error = ESP_OK;
    if (!g_vibe.cache.persisted || checkpoint_due) {
        if (g_vibe.cache.persisted) mark_cache_dirty();
        store_error = write_cache_now(now_utc);
    }
    const esp_err_t snapshot_error = vibe_snapshot(VIBE_WINDOW_TODAY, snapshot);
    if (snapshot_error != ESP_OK) return snapshot_error;
    if (store_error != ESP_OK) {
        snapshot->persisted = false;
        set_error(VIBE_ERR_STORAGE, 200);
        return store_error;
    }
    set_error(VIBE_OK, 200);
    return ESP_OK;
}

esp_err_t vibe_begin_reconcile(bool include_today) {
    if (!g_vibe.initialized) return ESP_ERR_INVALID_STATE;
    int32_t today;
    esp_err_t error = current_day(&today);
    if (error != ESP_OK) return error;
    const uint8_t next_mask =
        include_today || vibe_cache_find_day(&g_vibe.cache, today) == NULL
            ? 0x7fU
            : 0x7eU;
    if (g_vibe.cache.reconcile_anchor_key != today ||
        g_vibe.cache.reconcile_pending_mask != next_mask) {
        g_vibe.cache.reconcile_anchor_key = today;
        g_vibe.cache.reconcile_pending_mask = next_mask;
        mark_cache_dirty();
    }
    error = g_vibe.cache.persisted
                ? ESP_OK
                : write_cache_now((int64_t)time(NULL));
    set_error(error == ESP_OK ? VIBE_OK : VIBE_ERR_STORAGE, 0);
    return error;
}

esp_err_t vibe_reconcile(vibe_usage_snapshot_t *snapshot) {
    if (!g_vibe.initialized) return ESP_ERR_INVALID_STATE;
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    int32_t today;
    esp_err_t error = current_day(&today);
    if (error != ESP_OK) return error;
    if (g_vibe.cache.reconcile_anchor_key != today) {
        g_vibe.cache.reconcile_anchor_key = today;
        g_vibe.cache.reconcile_pending_mask =
            vibe_cache_find_day(&g_vibe.cache, today) == NULL ? 0x7fU : 0x7eU;
    }
    if (g_vibe.cache.reconcile_pending_mask == 0) {
        error = vibe_snapshot(VIBE_WINDOW_SEVEN_DAYS, snapshot);
        if (error == ESP_OK) set_error(VIBE_OK, 200);
        return error;
    }
    unsigned age = 0;
    while (age < 7U &&
           (g_vibe.cache.reconcile_pending_mask & (1U << age)) == 0) {
        ++age;
    }
    if (age >= 7U) return vibe_snapshot(VIBE_WINDOW_SEVEN_DAYS, snapshot);
    int32_t date_key;
    if (vibe_date_add_days(today, -(int)age, &date_key) != VIBE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    error = fetch_day(date_key);
    if (error != ESP_OK) return error;
    g_vibe.cache.reconcile_pending_mask &= (uint8_t)~(1U << age);
    if (g_vibe.cache.reconcile_pending_mask == 0) {
        g_vibe.cache.last_reconcile_at = (int64_t)time(NULL);
    }
    mark_cache_dirty();
    esp_err_t store_error = write_cache_now((int64_t)time(NULL));
    esp_err_t snapshot_error = vibe_snapshot(VIBE_WINDOW_SEVEN_DAYS, snapshot);
    if (snapshot_error != ESP_OK) return snapshot_error;
    if (store_error != ESP_OK) {
        snapshot->persisted = false;
        set_error(VIBE_ERR_STORAGE, 200);
        return store_error;
    }
    set_error(VIBE_OK, 200);
    return ESP_OK;
}

bool vibe_reconcile_pending(void) {
    return g_vibe.initialized && g_vibe.cache.reconcile_pending_mask != 0;
}

esp_err_t vibe_load_cached(vibe_usage_snapshot_t *snapshot) {
    return vibe_snapshot(VIBE_WINDOW_TODAY, snapshot);
}

esp_err_t vibe_snapshot(vibe_window_t window,
                        vibe_usage_snapshot_t *snapshot) {
    if (!g_vibe.initialized || snapshot == NULL) return ESP_ERR_INVALID_STATE;
    int32_t today;
    esp_err_t error = current_day(&today);
    if (error != ESP_OK) return error;
    return vibe_cache_build_snapshot(&g_vibe.cache, today, window, snapshot) ==
                   VIBE_OK
               ? ESP_OK
               : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t vibe_unlink(void) {
    if (!g_vibe.initialized) return ESP_ERR_INVALID_STATE;
    vibe_cancel();
    const uint32_t next_generation = next_account_generation();
    vibe_auth_record_t tombstone = {
        .present = true,
        .tombstone = true,
        .generation = next_generation,
    };
    esp_err_t error = vibe_store_write_auth(&tombstone);

    /* Stop using and displaying the old account even if NVS is unhealthy. */
    memset(&g_vibe.auth, 0, sizeof(g_vibe.auth));
    g_vibe.auth = tombstone;
    vibe_cache_init(&g_vibe.cache, tombstone.generation,
                    g_vibe.config_revision, g_vibe.timezone);
    g_vibe.retry_not_before_utc = 0;
    if (error == ESP_OK) error = vibe_store_clear_cache();
    if (error == ESP_OK) error = vibe_store_clear_retry_not_before();
    set_error(error == ESP_OK ? VIBE_OK : VIBE_ERR_STORAGE,
              g_vibe.last_http_status);
    return error;
}

esp_err_t vibe_factory_reset(void) {
    if (!g_vibe.initialized) return ESP_ERR_INVALID_STATE;
    vibe_cancel();
    esp_err_t error = vibe_store_begin_reset();
    if (error == ESP_OK) error = vibe_store_finish_reset();
    if (error == ESP_OK) memset(&g_vibe, 0, sizeof(g_vibe));
    return error;
}

esp_err_t vibe_factory_reset_pending(bool *pending) {
    return vibe_store_reset_pending(pending);
}

esp_err_t vibe_factory_reset_resume_local(void) {
    esp_err_t error = vibe_store_begin_reset();
    if (error == ESP_OK) error = vibe_store_finish_reset();
    return error;
}

esp_err_t vibe_factory_reset_complete(void) {
    return vibe_store_complete_reset();
}

void vibe_cancel(void) {
    (void)atomic_fetch_add(&g_vibe.cancel_generation, 1U);
}

bool vibe_has_auth(void) {
    return g_vibe.initialized && g_vibe.auth.present &&
           !g_vibe.auth.tombstone && g_vibe.auth.api_key[0] != '\0';
}

uint32_t vibe_account_generation(void) {
    return g_vibe.initialized ? g_vibe.auth.generation : 0;
}

vibe_error_t vibe_last_error(void) { return g_vibe.last_error; }

int vibe_last_http_status(void) { return g_vibe.last_http_status; }

uint32_t vibe_last_retry_after_seconds(void) {
    return g_vibe.last_retry_after_seconds;
}

uint32_t vibe_retry_remaining_seconds(void) {
    if (!g_vibe.initialized || g_vibe.retry_not_before_utc <= 0) return 0;
    const int64_t now_utc = (int64_t)time(NULL);
    if (now_utc >= g_vibe.retry_not_before_utc) return 0;
    const uint64_t remaining =
        (uint64_t)(g_vibe.retry_not_before_utc - now_utc);
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}
