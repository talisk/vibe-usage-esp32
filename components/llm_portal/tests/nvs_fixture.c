#include "nvs_fixture.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>
static unsigned char saved[2048], pending[2048];
static size_t saved_size, pending_size;
static char saved_language[16], pending_language[16];
static bool changed, language_present, language_changed, language_erased;
int fail_open, fail_read, fail_write, fail_commit;
void fixture_reset(void) {
    saved_size = pending_size = 0; changed = false;
    language_present = language_changed = language_erased = false;
    memset(saved_language, 0, sizeof(saved_language));
    fail_open = fail_read = fail_write = fail_commit = 0;
}
void fixture_corrupt(void) { assert(saved_size); saved[8] ^= 1; }
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *handle) {
    assert(!strcmp(ns, "vibe_llm")); (void)mode;
    if (fail_open) return fail_open;
    *handle = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) {
    assert(handle == 1); changed = language_changed = language_erased = false;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size) {
    assert(handle == 1 && !strcmp(key, "settings_v1"));
    if (fail_read) return fail_read;
    if (!saved_size) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size >= saved_size); *size = saved_size; memcpy(out, saved, saved_size); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
    assert(handle == 1 && !strcmp(key, "settings_v1") && size <= sizeof(pending));
    if (fail_write) return fail_write;
    memcpy(pending, data, size); pending_size = size; changed = true; return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *size) {
    assert(handle == 1 && !strcmp(key, "asr_lang_v1"));
    if (fail_read) return fail_read;
    if (!language_present) return ESP_ERR_NVS_NOT_FOUND;
    size_t required = strlen(saved_language) + 1;
    if (*size < required) return ESP_ERR_INVALID_SIZE;
    memcpy(out, saved_language, required); *size = required; return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
    assert(handle == 1 && !strcmp(key, "asr_lang_v1") && strlen(value) < sizeof(pending_language));
    if (fail_write) return fail_write;
    strcpy(pending_language, value); language_changed = true; language_erased = false; return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    assert(handle == 1);
    if (fail_write) return fail_write;
    if (!strcmp(key, "settings_v1")) {
        if (!saved_size) return ESP_ERR_NVS_NOT_FOUND;
        pending_size = 0; changed = true; return ESP_OK;
    }
    assert(!strcmp(key, "asr_lang_v1"));
    if (!language_present) return ESP_ERR_NVS_NOT_FOUND;
    language_erased = true; language_changed = false; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == 1);
    if (fail_commit) return fail_commit;
    if (changed) { memcpy(saved, pending, pending_size); saved_size = pending_size; }
    if (language_erased) { language_present = false; saved_language[0] = 0; }
    else if (language_changed) { strcpy(saved_language, pending_language); language_present = true; }
    changed = language_changed = language_erased = false; return ESP_OK;
}
