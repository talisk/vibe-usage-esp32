#include "vibe_internal.h"

#include <stdlib.h>
#include <string.h>

#include "nvs.h"

#define AUTH_BLOB_MAX_BYTES 2048U

static void put_u16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static void put_u32(uint8_t *output, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) output[i] = (uint8_t)(value >> (i * 8U));
}

static uint16_t get_u16(const uint8_t *input) {
    return (uint16_t)input[0] | (uint16_t)((uint16_t)input[1] << 8U);
}

static uint32_t get_u32(const uint8_t *input) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8U) |
           ((uint32_t)input[2] << 16U) | ((uint32_t)input[3] << 24U);
}

static size_t bounded_strlen(const char *text, size_t limit) {
    size_t length = 0;
    while (length < limit && text[length] != '\0') ++length;
    return length;
}

static esp_err_t encode_auth(const vibe_auth_record_t *record, uint8_t *output,
                             size_t output_size, size_t *written) {
    if (record == NULL || output == NULL || written == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t origin_length = bounded_strlen(record->origin,
                                                sizeof(record->origin));
    const size_t key_length = bounded_strlen(record->api_key,
                                             sizeof(record->api_key));
    if (origin_length >= sizeof(record->origin) ||
        key_length >= sizeof(record->api_key) ||
        record->generation == 0 ||
        (record->tombstone && (origin_length != 0 || key_length != 0)) ||
        (!record->tombstone && (origin_length == 0 || key_length == 0))) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t needed = 4U + 4U + 4U + 1U + 2U + 2U + origin_length +
                          key_length + 4U;
    if (needed > output_size || needed > AUTH_BLOB_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t offset = 0;
    memcpy(output + offset, "VUA1", 4);
    offset += 4;
    put_u32(output + offset, VIBE_SCHEMA_VERSION);
    offset += 4;
    put_u32(output + offset, record->generation);
    offset += 4;
    output[offset++] = record->tombstone ? 1U : 0U;
    put_u16(output + offset, (uint16_t)origin_length);
    offset += 2;
    put_u16(output + offset, (uint16_t)key_length);
    offset += 2;
    memcpy(output + offset, record->origin, origin_length);
    offset += origin_length;
    memcpy(output + offset, record->api_key, key_length);
    offset += key_length;
    put_u32(output + offset, vibe_crc32(output, offset));
    offset += 4;
    *written = offset;
    return ESP_OK;
}

static esp_err_t decode_auth(const uint8_t *input, size_t input_size,
                             vibe_auth_record_t *record) {
    if (input == NULL || record == NULL || input_size < 21U ||
        input_size > AUTH_BLOB_MAX_BYTES || memcmp(input, "VUA1", 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (get_u32(input + 4) != VIBE_SCHEMA_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    const uint32_t expected_crc = get_u32(input + input_size - 4U);
    if (vibe_crc32(input, input_size - 4U) != expected_crc) {
        return ESP_ERR_INVALID_CRC;
    }
    const bool tombstone = input[12] != 0;
    const size_t origin_length = get_u16(input + 13);
    const size_t key_length = get_u16(input + 15);
    if (origin_length >= VIBE_ORIGIN_BYTES || key_length >= VIBE_API_KEY_BYTES ||
        17U + origin_length + key_length + 4U != input_size ||
        get_u32(input + 8) == 0 ||
        (tombstone && (origin_length != 0 || key_length != 0)) ||
        (!tombstone && (origin_length == 0 || key_length == 0))) {
        return ESP_ERR_INVALID_SIZE;
    }
    memset(record, 0, sizeof(*record));
    record->present = true;
    record->tombstone = tombstone;
    record->generation = get_u32(input + 8);
    memcpy(record->origin, input + 17, origin_length);
    memcpy(record->api_key, input + 17 + origin_length, key_length);
    return ESP_OK;
}

esp_err_t vibe_store_load_auth(vibe_auth_record_t *record) {
    if (record == NULL) return ESP_ERR_INVALID_ARG;
    memset(record, 0, sizeof(*record));
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_auth", NVS_READONLY, &handle);
    if (error != ESP_OK) return error;
    size_t size = 0;
    error = nvs_get_blob(handle, "auth_v1", NULL, &size);
    if (error != ESP_OK || size > AUTH_BLOB_MAX_BYTES) {
        nvs_close(handle);
        return error == ESP_OK ? ESP_ERR_INVALID_SIZE : error;
    }
    uint8_t *blob = (uint8_t *)malloc(size);
    if (blob == NULL) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    error = nvs_get_blob(handle, "auth_v1", blob, &size);
    nvs_close(handle);
    if (error == ESP_OK) error = decode_auth(blob, size, record);
    memset(blob, 0, size);
    free(blob);
    return error;
}

esp_err_t vibe_store_write_auth(const vibe_auth_record_t *record) {
    uint8_t *blob = (uint8_t *)malloc(AUTH_BLOB_MAX_BYTES);
    if (blob == NULL) return ESP_ERR_NO_MEM;
    size_t size = 0;
    esp_err_t error = encode_auth(record, blob, AUTH_BLOB_MAX_BYTES, &size);
    if (error != ESP_OK) {
        memset(blob, 0, AUTH_BLOB_MAX_BYTES);
        free(blob);
        return error;
    }
    nvs_handle_t handle = 0;
    error = nvs_open("vibe_auth", NVS_READWRITE, &handle);
    if (error == ESP_OK) error = nvs_set_blob(handle, "auth_v1", blob, size);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    memset(blob, 0, AUTH_BLOB_MAX_BYTES);
    free(blob);
    return error;
}

static esp_err_t load_cache_key(nvs_handle_t handle, const char *key,
                                vibe_cache_t *cache) {
    size_t size = 0;
    esp_err_t error = nvs_get_blob(handle, key, NULL, &size);
    if (error != ESP_OK) return error;
    if (size == 0 || size > VIBE_CACHE_BLOB_MAX_BYTES) return ESP_ERR_INVALID_SIZE;
    uint8_t *blob = (uint8_t *)malloc(size);
    if (blob == NULL) return ESP_ERR_NO_MEM;
    error = nvs_get_blob(handle, key, blob, &size);
    const vibe_error_t decode_error =
        error == ESP_OK ? vibe_cache_decode(blob, size, cache) : VIBE_OK;
    memset(blob, 0, size);
    free(blob);
    if (error != ESP_OK) return error;
    if (decode_error == VIBE_ERR_NO_MEMORY) return ESP_ERR_NO_MEM;
    return decode_error == VIBE_OK ? ESP_OK : ESP_ERR_INVALID_CRC;
}

esp_err_t vibe_store_load_cache(vibe_cache_t *cache,
                                const vibe_auth_record_t *auth,
                                uint32_t config_revision,
                                vibe_timezone_t timezone) {
    if (cache == NULL || auth == NULL || !auth->present || auth->tombstone) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle;
    esp_err_t error = nvs_open("vibe_cache", NVS_READONLY, &handle);
    if (error != ESP_OK) return error;
    vibe_cache_t *slots = (vibe_cache_t *)calloc(2U, sizeof(*slots));
    if (slots == NULL) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t first_error = load_cache_key(handle, "cache_a", &slots[0]);
    const esp_err_t second_error = load_cache_key(handle, "cache_b", &slots[1]);
    nvs_close(handle);

    const bool first_compatible =
        first_error == ESP_OK && slots[0].generation == auth->generation &&
        slots[0].config_revision == config_revision &&
        slots[0].timezone == timezone &&
        slots[0].metric_id == VIBE_METRIC_ID_API_TOTAL_V1;
    const bool second_compatible =
        second_error == ESP_OK && slots[1].generation == auth->generation &&
        slots[1].config_revision == config_revision &&
        slots[1].timezone == timezone &&
        slots[1].metric_id == VIBE_METRIC_ID_API_TOTAL_V1;
    vibe_cache_t *selected = NULL;
    if (first_compatible) selected = &slots[0];
    if (second_compatible &&
        (selected == NULL || slots[1].sequence > selected->sequence)) {
        selected = &slots[1];
    }
    if (selected == NULL && (first_error == ESP_OK || second_error == ESP_OK)) {
        memset(slots, 0, 2U * sizeof(*slots));
        free(slots);
        return ESP_ERR_INVALID_VERSION;
    }
    if (selected == NULL) {
        memset(slots, 0, 2U * sizeof(*slots));
        free(slots);
        return ESP_ERR_NOT_FOUND;
    }
    *cache = *selected;
    memset(slots, 0, 2U * sizeof(*slots));
    free(slots);
    return ESP_OK;
}

esp_err_t vibe_store_write_cache(vibe_cache_t *cache) {
    if (cache == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t *blob = (uint8_t *)malloc(VIBE_CACHE_BLOB_MAX_BYTES);
    vibe_cache_t *verified = (vibe_cache_t *)malloc(sizeof(*verified));
    if (blob == NULL || verified == NULL) {
        free(blob);
        free(verified);
        return ESP_ERR_NO_MEM;
    }
    size_t size = 0;
    vibe_error_t encode_error =
        vibe_cache_encode(cache, blob, VIBE_CACHE_BLOB_MAX_BYTES, &size);
    if (encode_error != VIBE_OK) {
        memset(blob, 0, VIBE_CACHE_BLOB_MAX_BYTES);
        memset(verified, 0, sizeof(*verified));
        free(blob);
        free(verified);
        return ESP_ERR_INVALID_SIZE;
    }
    const char *key = (cache->sequence & 1U) == 0 ? "cache_a" : "cache_b";
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_cache", NVS_READWRITE, &handle);
    if (error == ESP_OK) error = nvs_set_blob(handle, key, blob, size);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (error == ESP_OK) {
        error = load_cache_key(handle, key, verified);
        if (error == ESP_OK && verified->sequence != cache->sequence) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (handle != 0) nvs_close(handle);
    memset(blob, 0, VIBE_CACHE_BLOB_MAX_BYTES);
    memset(verified, 0, sizeof(*verified));
    free(blob);
    free(verified);
    if (error == ESP_OK) cache->persisted = true;
    return error;
}

esp_err_t vibe_store_clear_cache(void) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_cache", NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    esp_err_t first = nvs_erase_key(handle, "cache_a");
    esp_err_t second = nvs_erase_key(handle, "cache_b");
    if (first != ESP_OK && first != ESP_ERR_NVS_NOT_FOUND) error = first;
    if (second != ESP_OK && second != ESP_ERR_NVS_NOT_FOUND) error = second;
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

esp_err_t vibe_store_begin_reset(void) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_meta", NVS_READWRITE, &handle);
    if (error == ESP_OK) error = nvs_set_u8(handle, "reset_v1", 1);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return error;
}

static esp_err_t erase_namespace(const char *name) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(name, NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    error = nvs_erase_all(handle);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

esp_err_t vibe_store_finish_reset(void) {
    esp_err_t error = erase_namespace("vibe_auth");
    if (error == ESP_OK) error = erase_namespace("vibe_cache");
    if (error == ESP_OK) error = erase_namespace("vibe_cfg");
    if (error == ESP_OK) error = vibe_store_clear_retry_not_before();
    return error;
}

esp_err_t vibe_store_load_retry_not_before(int64_t *utc_seconds) {
    if (utc_seconds == NULL) return ESP_ERR_INVALID_ARG;
    *utc_seconds = 0;
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_meta", NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    error = nvs_get_i64(handle, "retry_v1", utc_seconds);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        *utc_seconds = 0;
        return ESP_OK;
    }
    if (error == ESP_OK && *utc_seconds < 0) return ESP_ERR_INVALID_STATE;
    return error;
}

esp_err_t vibe_store_write_retry_not_before(int64_t utc_seconds) {
    if (utc_seconds <= 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_meta", NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_i64(handle, "retry_v1", utc_seconds);
    }
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return error;
}

esp_err_t vibe_store_clear_retry_not_before(void) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_meta", NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    error = nvs_erase_key(handle, "retry_v1");
    if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

esp_err_t vibe_store_reset_pending(bool *pending) {
    if (pending == NULL) return ESP_ERR_INVALID_ARG;
    *pending = false;
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_meta", NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    uint8_t value = 0;
    error = nvs_get_u8(handle, "reset_v1", &value);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error == ESP_OK) *pending = value == 1;
    return error;
}

esp_err_t vibe_store_complete_reset(void) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_meta", NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    error = nvs_erase_key(handle, "reset_v1");
    if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}
