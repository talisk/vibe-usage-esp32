#include "llm_portal.h"
#include "llm_endpoint.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "nvs.h"

/* Explicit byte representation keeps schema independent of bool/padding ABI. */
typedef struct {
    uint8_t magic[4];
    uint8_t enabled;
    uint8_t reserved[3];
    char chat_url[256], chat_model[64], api_key[256];
    char asr_url[256], asr_model[64], asr_key[256];
    uint32_t crc32;
} stored_llm_t;

static void wipe(void *data, size_t size) {
    volatile uint8_t *p = data;
    while (size--) *p++ = 0;
}
static uint32_t crc32(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return ~crc;
}
static bool field(const char *text, size_t capacity, bool required) {
    for (size_t i = 0; i < capacity; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!c) return !required || i > 0;
        if (c < 0x20 || c == 0x7f) return false;
    }
    return false;
}
static bool language_field(const char *text, size_t capacity) {
    if (!field(text, capacity, false)) return false;
    for (size_t i = 0; text[i]; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}
bool llm_settings_valid(const llm_settings_t *settings) {
    return settings &&
           settings->asr_upload >= LLM_ASR_UPLOAD_AUTO &&
           settings->asr_upload <= LLM_ASR_UPLOAD_BASE64_JSON &&
           field(settings->chat_url, sizeof(settings->chat_url), true) &&
           field(settings->asr_url, sizeof(settings->asr_url), true) &&
           llm_endpoint_valid(settings->chat_url) && llm_endpoint_valid(settings->asr_url) &&
           field(settings->chat_model, sizeof(settings->chat_model), true) &&
           field(settings->asr_model, sizeof(settings->asr_model), true) &&
           language_field(settings->asr_language, sizeof(settings->asr_language)) &&
           field(settings->api_key, sizeof(settings->api_key), false) &&
           field(settings->asr_key, sizeof(settings->asr_key), false);
}
const char *llm_asr_upload_name(llm_asr_upload_t upload) {
    if (upload == LLM_ASR_UPLOAD_AUTO) return "auto";
    if (upload == LLM_ASR_UPLOAD_MULTIPART) return "multipart";
    if (upload == LLM_ASR_UPLOAD_BASE64_JSON) return "base64_json";
    return NULL;
}
bool llm_asr_upload_parse(const char *name, llm_asr_upload_t *upload) {
    if (!name || !upload) return false;
    if (!strcmp(name, "auto")) *upload = LLM_ASR_UPLOAD_AUTO;
    else if (!strcmp(name, "multipart")) *upload = LLM_ASR_UPLOAD_MULTIPART;
    else if (!strcmp(name, "base64_json")) *upload = LLM_ASR_UPLOAD_BASE64_JSON;
    else return false;
    return true;
}
static void defaults(llm_settings_t *settings) {
    memset(settings, 0, sizeof(*settings));
    strcpy(settings->chat_url, "https://api.openai.com/v1/chat/completions");
    strcpy(settings->chat_model, "gpt-4o-mini");
    strcpy(settings->asr_url, "https://api.openai.com/v1/audio/transcriptions");
    strcpy(settings->asr_model, "gpt-4o-mini-transcribe");
    strcpy(settings->asr_language, "zh");
}
#define COPY_FIELDS(to, from) do { \
    memcpy((to)->chat_url, (from)->chat_url, sizeof((to)->chat_url)); \
    memcpy((to)->chat_model, (from)->chat_model, sizeof((to)->chat_model)); \
    memcpy((to)->api_key, (from)->api_key, sizeof((to)->api_key)); \
    memcpy((to)->asr_url, (from)->asr_url, sizeof((to)->asr_url)); \
    memcpy((to)->asr_model, (from)->asr_model, sizeof((to)->asr_model)); \
    memcpy((to)->asr_key, (from)->asr_key, sizeof((to)->asr_key)); \
} while (0)

esp_err_t llm_settings_load(llm_settings_t *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;
    memset(settings, 0, sizeof(*settings));
    stored_llm_t stored = {0};
    char asr_language[sizeof(settings->asr_language)] = "zh";
    nvs_handle_t handle = 0;
    size_t size = sizeof(stored);
    esp_err_t error = nvs_open("vibe_llm", NVS_READONLY, &handle);
    if (error == ESP_OK) error = nvs_get_blob(handle, "settings_v1", &stored, &size);
    if (error == ESP_OK) {
        size_t language_size = sizeof(asr_language);
        esp_err_t language_error = nvs_get_str(handle, "asr_lang_v1", asr_language,
                                               &language_size);
        if (language_error != ESP_OK && language_error != ESP_ERR_NVS_NOT_FOUND)
            error = language_error;
    }
    if (handle) nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        defaults(settings);
        error = ESP_OK;
    } else if (error == ESP_OK) {
        if (size != sizeof(stored) || memcmp(stored.magic, "VLM1", 4) || stored.enabled > 1 ||
            stored.reserved[0] > LLM_ASR_UPLOAD_BASE64_JSON || stored.reserved[1] || stored.reserved[2] ||
            crc32(&stored, offsetof(stored_llm_t, crc32)) != stored.crc32) {
            error = ESP_ERR_INVALID_CRC;
        } else {
            settings->enabled = stored.enabled != 0;
            settings->asr_upload = (llm_asr_upload_t)stored.reserved[0];
            COPY_FIELDS(settings, &stored);
            memcpy(settings->asr_language, asr_language, sizeof(settings->asr_language));
            if (!llm_settings_valid(settings)) error = ESP_ERR_INVALID_CRC;
        }
    }
    wipe(&stored, sizeof(stored));
    wipe(asr_language, sizeof(asr_language));
    if (error != ESP_OK) wipe(settings, sizeof(*settings));
    return error;
}
esp_err_t llm_settings_save(const llm_settings_t *settings) {
    if (!llm_settings_valid(settings)) return ESP_ERR_INVALID_ARG;
    stored_llm_t stored = {.magic = {'V', 'L', 'M', '1'}, .enabled = settings->enabled ? 1 : 0};
    stored.reserved[0] = (uint8_t)settings->asr_upload;
    COPY_FIELDS(&stored, settings);
    stored.crc32 = crc32(&stored, offsetof(stored_llm_t, crc32));
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_llm", NVS_READWRITE, &handle);
    if (error == ESP_OK) error = nvs_set_blob(handle, "settings_v1", &stored, sizeof(stored));
    if (error == ESP_OK) error = nvs_set_str(handle, "asr_lang_v1", settings->asr_language);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle) nvs_close(handle);
    wipe(&stored, sizeof(stored));
    return error;
}
esp_err_t llm_settings_clear(void) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("vibe_llm", NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error == ESP_OK) {
        esp_err_t first = nvs_erase_key(handle, "settings_v1");
        esp_err_t second = nvs_erase_key(handle, "asr_lang_v1");
        if (first != ESP_OK && first != ESP_ERR_NVS_NOT_FOUND) error = first;
        else if (second != ESP_OK && second != ESP_ERR_NVS_NOT_FOUND) error = second;
    }
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle) nvs_close(handle);
    return error;
}
