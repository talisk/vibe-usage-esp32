#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "device_config.h"
#include "nvs.h"

static unsigned char blob[64];
static size_t blob_size;
static uint8_t saved_language, staged_language;
static int language_present, staged, fail_commit, fail_read, fail_write;
uint32_t esp_random(void) { return 0x1234; }
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) {
    assert(strcmp(ns, "vibe_cfg") == 0); (void)mode; *h = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h == 1); staged = 0; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size) {
    assert(h == 1 && strcmp(key, "settings_v1") == 0);
    if (!blob_size) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size >= blob_size); memcpy(out, blob, blob_size); *size = blob_size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *in, size_t size) {
    assert(h == 1 && strcmp(key, "settings_v1") == 0 && size <= sizeof(blob));
    memcpy(blob, in, size); blob_size = size; return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out) {
    assert(h == 1 && strcmp(key, "language_v1") == 0);
    if (fail_read) return fail_read;
    if (!language_present) return ESP_ERR_NVS_NOT_FOUND;
    *out = saved_language; return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) {
    assert(h == 1 && strcmp(key, "language_v1") == 0);
    if (fail_write) return ESP_FAIL;
    staged_language = value; staged = 1; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h == 1);
    if (fail_commit) return ESP_FAIL;
    if (staged) { saved_language = staged_language; language_present = 1; }
    return ESP_OK;
}
int main(void) {
    device_config_t config, restored;
    assert(device_config_load_or_create(300, &config) == ESP_OK);
    assert(config.language == VIBE_LANG_EN && !language_present);
    unsigned char original[64]; memcpy(original, blob, blob_size);
    for (int language = 0; language < VIBE_LANG_COUNT; ++language) {
        assert(device_config_save_language((vibe_language_t)language) == ESP_OK);
        assert(device_config_load_or_create(300, &restored) == ESP_OK);
        assert(restored.language == (vibe_language_t)language);
        assert(memcmp(original, blob, blob_size) == 0);
        assert(restored.config_revision == config.config_revision);
        assert(restored.timezone == config.timezone);
        assert(restored.refresh_seconds == config.refresh_seconds);
        assert(strcmp(restored.device_id, config.device_id) == 0);
    }
    // Existing schema-1 blob remains byte-identical, including its CRC and ID.
    assert(blob_size == 26);
    fail_commit = 1;
    assert(device_config_save_language(VIBE_LANG_EN) == ESP_FAIL);
    fail_commit = 0;
    fail_write = 1;
    assert(device_config_save_language(VIBE_LANG_EN) == ESP_FAIL);
    fail_write = 0;
    assert(device_config_load_or_create(300, &restored) == ESP_OK);
    assert(restored.language == VIBE_LANG_JA);
    assert(device_config_save_language(VIBE_LANG_COUNT) == ESP_ERR_INVALID_ARG);
    assert(device_config_save_language((vibe_language_t)-1) == ESP_ERR_INVALID_ARG);
    saved_language = 255;
    assert(device_config_load_or_create(300, &restored) == ESP_OK);
    assert(restored.language == VIBE_LANG_EN);
    fail_read = ESP_ERR_NVS_TYPE_MISMATCH;
    assert(device_config_load_or_create(300, &restored) == ESP_OK);
    assert(restored.language == VIBE_LANG_EN);
    fail_read = ESP_FAIL;
    assert(device_config_load_or_create(300, &restored) == ESP_FAIL);
    assert(memcmp(original, blob, blob_size) == 0);
    puts("Language persistence / old config migration / storage failure: PASS");
}
