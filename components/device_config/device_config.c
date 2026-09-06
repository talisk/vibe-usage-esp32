#include "device_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "nvs.h"
#include "vibe_cache.h"

typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    uint32_t schema_version;
    uint32_t config_revision;
    uint32_t refresh_seconds;
    uint8_t timezone;
    char device_id[5];
    uint32_t crc32;
} stored_config_t;

static size_t bounded_strlen(const char *text, size_t capacity) {
    size_t length = 0;
    if (text == NULL) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static bool valid(const stored_config_t *stored) {
    return stored != NULL && memcmp(stored->magic, "VUF1", 4) == 0 &&
           stored->schema_version == VIBE_SCHEMA_VERSION &&
           stored->config_revision != 0 && stored->refresh_seconds >= 60 &&
           stored->timezone <= VIBE_TZ_UTC && stored->device_id[4] == '\0' &&
           bounded_strlen(stored->device_id, sizeof(stored->device_id)) == 4 &&
           vibe_crc32((const uint8_t *)stored,
                      sizeof(*stored) - sizeof(stored->crc32)) == stored->crc32;
}

esp_err_t device_config_save(device_config_t *config) {
    if (config == NULL || config->config_revision == 0 ||
        config->refresh_seconds < 60 || config->timezone > VIBE_TZ_UTC ||
        bounded_strlen(config->device_id, sizeof(config->device_id)) != 4) {
        return ESP_ERR_INVALID_ARG;
    }
    stored_config_t stored = {
        .magic = {'V', 'U', 'F', '1'},
        .schema_version = VIBE_SCHEMA_VERSION,
        .config_revision = config->config_revision,
        .refresh_seconds = config->refresh_seconds,
        .timezone = (uint8_t)config->timezone,
    };
    memcpy(stored.device_id, config->device_id, sizeof(stored.device_id));
    stored.crc32 = vibe_crc32((const uint8_t *)&stored,
                              sizeof(stored) - sizeof(stored.crc32));
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_cfg", NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, "settings_v1", &stored, sizeof(stored));
    }
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    memset(&stored, 0, sizeof(stored));
    return error;
}

esp_err_t device_config_save_language(vibe_language_t language) {
    if (language < VIBE_LANG_EN || language >= VIBE_LANG_COUNT) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_cfg", NVS_READWRITE, &handle);
    if (error == ESP_OK) error = nvs_set_u8(handle, "language_v1", (uint8_t)language);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return error;
}

esp_err_t device_config_save_alert_volume(uint8_t volume) {
    if (volume > 100) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_cfg", NVS_READWRITE, &handle);
    if (error == ESP_OK) error = nvs_set_u8(handle, "alert_vol_v1", volume);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return error;
}

esp_err_t device_config_load_or_create(uint32_t default_refresh_seconds,
                                       device_config_t *config) {
    if (config == NULL || default_refresh_seconds < 60) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle = 0;
    stored_config_t stored;
    uint8_t language = VIBE_LANG_EN;
    uint8_t alert_volume = DEVICE_ALERT_VOLUME_DEFAULT;
    size_t size = sizeof(stored);
    esp_err_t error = nvs_open("vibe_cfg", NVS_READONLY, &handle);
    if (error == ESP_OK) {
        error = nvs_get_blob(handle, "settings_v1", &stored, &size);
        if (error == ESP_OK) {
            esp_err_t lang_error = nvs_get_u8(handle, "language_v1", &language);
            if (lang_error == ESP_ERR_NVS_NOT_FOUND || lang_error == ESP_ERR_NVS_TYPE_MISMATCH ||
                language >= VIBE_LANG_COUNT) language = VIBE_LANG_EN;
            else if (lang_error != ESP_OK) error = lang_error;
            if (error == ESP_OK) {
                esp_err_t volume_error = nvs_get_u8(handle, "alert_vol_v1", &alert_volume);
                if (volume_error == ESP_ERR_NVS_NOT_FOUND ||
                    volume_error == ESP_ERR_NVS_TYPE_MISMATCH || alert_volume > 100) {
                    alert_volume = DEVICE_ALERT_VOLUME_DEFAULT;
                } else if (volume_error != ESP_OK) {
                    error = volume_error;
                }
            }
        }
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        if (size != sizeof(stored) || !valid(&stored)) {
            memset(&stored, 0, sizeof(stored));
            return ESP_ERR_INVALID_CRC;
        }
        memset(config, 0, sizeof(*config));
        config->schema_version = stored.schema_version;
        config->config_revision = stored.config_revision;
        config->refresh_seconds = stored.refresh_seconds;
        config->timezone = (vibe_timezone_t)stored.timezone;
        config->language = (vibe_language_t)language;
        config->alert_volume = alert_volume;
        memcpy(config->device_id, stored.device_id, sizeof(config->device_id));
        memset(&stored, 0, sizeof(stored));
        return ESP_OK;
    }
    if (error != ESP_ERR_NVS_NOT_FOUND) return error;

    memset(config, 0, sizeof(*config));
    config->schema_version = VIBE_SCHEMA_VERSION;
    config->config_revision = 1;
    config->refresh_seconds = default_refresh_seconds;
    config->timezone = VIBE_TZ_ASIA_SHANGHAI;
    config->alert_volume = DEVICE_ALERT_VOLUME_DEFAULT;
    const uint16_t random_id = (uint16_t)esp_random();
    snprintf(config->device_id, sizeof(config->device_id), "%04X", random_id);
    return device_config_save(config);
}
