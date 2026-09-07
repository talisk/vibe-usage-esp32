#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smart_todo_llm.h"
#include "board_services.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_partition.h"
#include "cJSON.h"
#include "nvs_fixture.h"

#define PCM_BYTES (8000U * 8U * 2U)
struct mock_http_client {
    esp_http_client_config_t config;
    char url[256], type[96], authorization[264];
    unsigned char upload[((PCM_BYTES + 44) * 4 / 3) + 8192];
    size_t uploaded, announced, read_offset;
    bool live, opened;
};
static struct mock_http_client client;
static int response_status, init_failure, header_failure, open_failure, write_failure;
static bool incomplete, error_after_body, capture_live, silent, initial_release, cancellation;
static bool chunked, last_header_failed;
static bool partition_write_failure, partition_read_failure;
static unsigned header_transport_failures, init_calls;
static size_t release_samples, captured_samples, read_error_at, write_fragment;
static size_t beep_calls, start_calls, stop_calls, recording_starts, recording_stops, cleanups;
static int64_t clock_us, declared_length;
static const char *response_body;
static size_t response_length;
static esp_err_t audio_failure;
static unsigned char voice_storage[PCM_BYTES];
static const esp_partition_t voice_partition = {.size = sizeof(voice_storage), .label = "voicebuf"};
static llm_settings_t settings;
static smart_todo_list_t list;
static size_t allocation_countdown;
static void *json_allocate(size_t size) {
    if (allocation_countdown == 0) return NULL;
    --allocation_countdown; return malloc(size);
}

int64_t esp_timer_get_time(void) { return clock_us; }
esp_err_t esp_crt_bundle_attach(void *configuration) { (void)configuration; return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    assert(!client.live);
    if (strstr(config->url, "transcriptions")) assert(!capture_live && stop_calls == 1);
    assert(config->method == HTTP_METHOD_POST && config->timeout_ms == 15000);
    assert(config->disable_auto_redirect && config->max_redirection_count == 0);
    assert(config->crt_bundle_attach == esp_crt_bundle_attach);
    if (init_failure) return NULL;
    client.uploaded = client.announced = client.read_offset = 0;
    client.opened = false; client.type[0] = 0; client.authorization[0] = 0;
    ++init_calls;
    client.config = *config; strcpy(client.url, config->url); client.live = true; return &client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t handle, const char *name, const char *value) {
    assert(handle == &client && client.live);
    if (header_failure) return ESP_FAIL;
    if (!strcmp(name, "Content-Type")) { assert(strlen(value) < sizeof(client.type)); strcpy(client.type, value); }
    else if (!strcmp(name, "Authorization")) { assert(strlen(value) < sizeof(client.authorization)); strcpy(client.authorization, value); }
    else if (!strcmp(name, "Accept")) assert(!strcmp(value, "application/json"));
    else if (!strcmp(name, "Accept-Encoding")) assert(!strcmp(value, "identity"));
    else assert(false);
    return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t handle, int timeout) {
    assert(handle == &client && client.live && !capture_live);
    assert(timeout == 200 || timeout == 15000 || timeout == 25000 || timeout == 90000);
    client.config.timeout_ms = timeout; return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t handle, int length) {
    assert(handle == &client && client.live && length > 0 && (size_t)length < sizeof(client.upload));
    if (open_failure) return ESP_FAIL;
    client.announced = (size_t)length; client.opened = true; return ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t handle, const char *data, int length) {
    assert(handle == &client && client.opened && length > 0);
    assert(!capture_live);
    if (write_failure) return -1;
    size_t amount = (size_t)length > write_fragment ? write_fragment : (size_t)length;
    assert(client.uploaded + amount <= client.announced);
    memcpy(client.upload + client.uploaded, data, amount); client.uploaded += amount;
    esp_http_client_event_t event = {.user_data = client.config.user_data};
    if (client.config.event_handler(&event) != ESP_OK) return -1;
    return (int)amount;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t handle) {
    assert(handle == &client && client.opened && client.uploaded == client.announced);
    assert(!capture_live);
    if (strstr(client.url, "transcriptions")) assert(client.config.timeout_ms == 90000);
    else assert(client.config.timeout_ms == 25000);
    last_header_failed = header_transport_failures > 0;
    if (last_header_failed) { --header_transport_failures; return -1; }
    return declared_length;
}
int esp_http_client_read(esp_http_client_handle_t handle, char *out, int capacity) {
    assert(handle == &client && capacity > 0);
    if (client.read_offset >= read_error_at || (error_after_body && client.read_offset == response_length)) return -1;
    size_t size = response_length - client.read_offset;
    if (size > 11) size = 11;
    if (size > (size_t)capacity) size = (size_t)capacity;
    memcpy(out, response_body + client.read_offset, size); client.read_offset += size; return (int)size;
}
int esp_http_client_get_status_code(esp_http_client_handle_t handle) { assert(handle == &client); return last_header_failed ? -1 : response_status; }
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t handle) {
    assert(handle == &client); return !incomplete && client.read_offset == response_length;
}
bool esp_http_client_is_chunked_response(esp_http_client_handle_t handle) {
    assert(handle == &client); return chunked;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t handle) { assert(handle == &client); client.opened = false; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle) {
    assert(handle == &client && client.live); client.live = false; ++cleanups; return ESP_OK;
}
esp_err_t board_audio_beep(void) { assert(!capture_live); ++beep_calls; return audio_failure; }
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
    esp_partition_subtype_t subtype, const char *label) {
    assert(type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_ANY);
    return !strcmp(label, voice_partition.label) ? &voice_partition : NULL;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size) {
    assert(partition == &voice_partition && offset == 0 && size == sizeof(voice_storage));
    memset(voice_storage, 0xff, sizeof(voice_storage)); return ESP_OK;
}
esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset,
                              const void *source, size_t size) {
    assert(partition == &voice_partition && offset + size <= sizeof(voice_storage));
    if (partition_write_failure) return ESP_FAIL;
    memcpy(voice_storage + offset, source, size); return ESP_OK;
}
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset,
                             void *destination, size_t size) {
    assert(partition == &voice_partition && offset + size <= sizeof(voice_storage));
    if (partition_read_failure) return ESP_FAIL;
    memcpy(destination, voice_storage + offset, size); return ESP_OK;
}
esp_err_t board_audio_start_capture(void) {
    assert(!capture_live); ++start_calls;
    if (audio_failure != ESP_OK) return audio_failure;
    capture_live = true; return ESP_OK;
}
esp_err_t board_audio_read(int16_t *out, size_t capacity, size_t *count, uint32_t timeout_ms) {
    assert(capture_live && timeout_ms <= 200 && capacity <= 256);
    if (audio_failure != ESP_OK) { *count = 0; return audio_failure; }
    size_t size = capacity > 128 ? 128 : capacity; /* Short board reads are legal. */
    for (size_t i = 0; i < size; ++i) out[i] = silent ? 0 : (i % 2 ? -1024 : 1024);
    captured_samples += size; *count = size; clock_us += (int64_t)size * 1000000 / 8000;
    return ESP_OK;
}
void board_audio_stop_capture(void) { assert(capture_live); capture_live = false; ++stop_calls; }
static bool held(void *context) { (void)context; return !initial_release && captured_samples < release_samples; }
static bool cancelled(void *context) { (void)context; return cancellation; }
static void recording(bool active, void *context) { (void)context; if (active) ++recording_starts; else ++recording_stops; }
static smart_todo_voice_callbacks_t callbacks = {.held = held, .cancelled = cancelled, .recording = recording};
static void reset(const char *body) {
    assert(!client.live && !capture_live);
    memset(&client, 0, sizeof(client));
    response_status = 200; init_failure = header_failure = open_failure = write_failure = 0;
    incomplete = error_after_body = silent = initial_release = cancellation = false;
    chunked = last_header_failed = false; header_transport_failures = init_calls = 0;
    partition_write_failure = partition_read_failure = false;
    release_samples = SIZE_MAX; captured_samples = 0; read_error_at = SIZE_MAX; write_fragment = 29;
    beep_calls = start_calls = stop_calls = recording_starts = recording_stops = cleanups = 0;
    clock_us = 0; audio_failure = ESP_OK;
    response_body = body; response_length = strlen(body); declared_length = (int64_t)response_length;
    fixture_reset(); assert(llm_settings_load(&settings) == ESP_OK); settings.enabled = true;
    strcpy(settings.api_key, "fixture-chat-key"); strcpy(settings.asr_key, "fixture-asr-key");
    smart_todo_init(&list);
}
static esp_err_t transcribe(char text[SMART_TODO_TRANSCRIPT_BYTES]) {
    int status = -1;
    esp_err_t error = smart_todo_transcribe(&settings, &callbacks, text, SMART_TODO_TRANSCRIPT_BYTES, &status);
    assert(!client.live && !capture_live);
    assert(recording_starts == recording_stops);
    if (error == ESP_OK) assert(status == 200);
    return error;
}
static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static size_t decode_base64(const char *input, unsigned char *output, size_t capacity) {
    size_t used = 0, length = strlen(input); assert(length % 4 == 0);
    for (size_t i = 0; i < length; i += 4) {
        int a = base64_value(input[i]), b = base64_value(input[i + 1]);
        int c = input[i + 2] == '=' ? 0 : base64_value(input[i + 2]);
        int d = input[i + 3] == '=' ? 0 : base64_value(input[i + 3]);
        assert(a >= 0 && b >= 0 && c >= 0 && d >= 0 && used < capacity);
        output[used++] = (unsigned char)(a << 2 | b >> 4);
        if (input[i + 2] != '=') { assert(used < capacity); output[used++] = (unsigned char)(b << 4 | c >> 2); }
        if (input[i + 3] != '=') { assert(used < capacity); output[used++] = (unsigned char)(c << 6 | d); }
    }
    return used;
}
static void inspect_wav(bool released) {
    assert(client.uploaded == client.announced && client.uploaded > captured_samples * 2 + 44);
    assert(!strcmp(client.authorization, "Bearer fixture-asr-key"));
    assert(strstr(client.type, "multipart/form-data; boundary=vibe_todo_audio_01"));
    if (settings.asr_language[0]) {
        char language[64];
        snprintf(language, sizeof(language), "name=\"language\"\r\n\r\n%s\r\n",
                 settings.asr_language);
        assert(strstr((const char *)client.upload, language));
    } else {
        assert(!strstr((const char *)client.upload, "name=\"language\""));
    }
    const unsigned char *wav = NULL;
    for (size_t i = 0; i + 4 < client.uploaded; ++i) if (!memcmp(client.upload + i, "RIFF", 4)) { wav = client.upload + i; break; }
    assert(wav && get32(wav + 4) == captured_samples * 2 + 36);
    assert(!memcmp(wav + 8, "WAVEfmt ", 8) && get32(wav + 16) == 16);
    assert(wav[20] == 1 && wav[22] == 1 && wav[34] == 16);
    assert(get32(wav + 24) == 8000 && get32(wav + 28) == 16000);
    assert(!memcmp(wav + 36, "data", 4) && get32(wav + 40) == captured_samples * 2);
    const unsigned char *pcm = wav + 44;
    assert(pcm[0] == 0 && pcm[1] == 4 && pcm[2] == 0 && pcm[3] == 252);
    (void)released;
    assert(!memcmp(pcm + captured_samples * 2, "\r\n--vibe_todo_audio_01--\r\n", 25));
    assert(beep_calls == 1 && start_calls == 1 && stop_calls == 1);
}
static void inspect_openrouter(void) {
    assert(client.uploaded == client.announced && !strcmp(client.type, "application/json"));
    cJSON *root = cJSON_Parse((const char *)client.upload); assert(root);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(root, "model")->valuestring,
                   settings.asr_model));
    const cJSON *language = cJSON_GetObjectItemCaseSensitive(root, "language");
    if (settings.asr_language[0])
        assert(cJSON_IsString(language) && !strcmp(language->valuestring, settings.asr_language));
    else
        assert(!language);
    cJSON *input = cJSON_GetObjectItemCaseSensitive(root, "input_audio");
    assert(cJSON_IsObject(input));
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(input, "data");
    assert(cJSON_IsString(data) && !strncmp(data->valuestring, "UklGR", 5));
    assert(strlen(data->valuestring) == ((44 + captured_samples * 2 + 2) / 3) * 4);
    unsigned char decoded[PCM_BYTES + 44];
    size_t decoded_length = decode_base64(data->valuestring, decoded, sizeof(decoded));
    assert(decoded_length == 44 + captured_samples * 2);
    assert(!memcmp(decoded, "RIFF", 4) && get32(decoded + 4) == captured_samples * 2 + 36);
    assert(!memcmp(decoded + 8, "WAVEfmt ", 8) && !memcmp(decoded + 36, "data", 4));
    assert(get32(decoded + 40) == captured_samples * 2);
    for (size_t i = 0; i < captured_samples; ++i)
        assert(decoded[44 + i * 2] == 0 && decoded[45 + i * 2] == (i % 2 ? 252 : 4));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(input, "format")->valuestring, "wav"));
    cJSON_Delete(root);
    assert(beep_calls == 1 && start_calls == 1 && stop_calls == 1);
}
static esp_err_t interpret(smart_todo_action_t *action) {
    int status = -1;
    esp_err_t error = smart_todo_interpret(&settings, &list, "Add buy milk, remind me in two hours", &callbacks, action, &status);
    assert(!client.live && !capture_live);
    return error;
}
#define VALID_CHAT "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":\"{\\\"action\\\":\\\"add\\\",\\\"title\\\":\\\"Buy milk\\\",\\\"delay_seconds\\\":7200,\\\"repeat_seconds\\\":0}\"}}]}"
#define BATCH_CHAT "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":\"{\\\"action\\\":\\\"complete\\\",\\\"ids\\\":[1,2,99]}\"}}]}"
static void inspect_chat(void) {
    assert(client.announced == client.uploaded && !strcmp(client.type, "application/json"));
    assert(!strcmp(client.authorization, "Bearer fixture-chat-key"));
    cJSON *body = cJSON_Parse((const char *)client.upload); assert(body);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(body, "model")->valuestring, settings.chat_model));
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(body, "stream")));
    assert(cJSON_GetObjectItemCaseSensitive(body, "max_tokens")->valueint == 1200);
    cJSON *format = cJSON_GetObjectItemCaseSensitive(body, "response_format");
    cJSON *reasoning = cJSON_GetObjectItemCaseSensitive(body, "reasoning");
    cJSON *plugins = cJSON_GetObjectItemCaseSensitive(body, "plugins");
    cJSON *provider = cJSON_GetObjectItemCaseSensitive(body, "provider");
    assert(cJSON_IsObject(format) &&
           !strcmp(cJSON_GetObjectItemCaseSensitive(format, "type")->valuestring, "json_object"));
    assert(cJSON_IsObject(reasoning) &&
           !strcmp(cJSON_GetObjectItemCaseSensitive(reasoning, "effort")->valuestring, "low") &&
           cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reasoning, "exclude")));
    assert(cJSON_IsArray(plugins) && cJSON_GetArraySize(plugins) == 1 &&
           !strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(plugins, 0), "id")->valuestring,
                   "response-healing"));
    assert(cJSON_IsObject(provider) &&
           cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(provider, "require_parameters")));
    assert(cJSON_GetObjectItemCaseSensitive(body, "temperature")->valuedouble == 0);
    cJSON *messages = cJSON_GetObjectItemCaseSensitive(body, "messages");
    assert(cJSON_IsArray(messages) && cJSON_GetArraySize(messages) == 2);
    const char *system = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(messages, 0), "content")->valuestring;
    assert(strstr(system, "\"ids\":[1,2]") && strstr(system, "123 已完成") && strstr(system, "删除 456"));
    cJSON *user = cJSON_GetArrayItem(messages, 1);
    cJSON *data = cJSON_Parse(cJSON_GetObjectItemCaseSensitive(user, "content")->valuestring); assert(data);
    assert(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(data, "items")));
    assert(strstr(cJSON_GetObjectItemCaseSensitive(data, "instruction")->valuestring, "two hours"));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(data, "current_local_time")));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(data, "utc_offset")));
    cJSON_Delete(data); cJSON_Delete(body);
}
int main(void) {
    char transcript[SMART_TODO_TRANSCRIPT_BYTES];
    reset("{\"text\":\"Buy milk\"}");
    assert(transcribe(transcript) == ESP_OK && !strcmp(transcript, "Buy milk"));
    assert(captured_samples == 64000); inspect_wav(false);
    reset("{\"text\":\"Buy milk\"}"); release_samples = 3200;
    assert(transcribe(transcript) == ESP_OK); assert(captured_samples == 3200); inspect_wav(true);
    reset("{\"text\":\"Buy milk\"}"); release_samples = 3200;
    strcpy(settings.asr_language, "en-US");
    assert(transcribe(transcript) == ESP_OK); inspect_wav(true);
    reset("{\"text\":\"Buy milk\"}"); release_samples = 3200;
    strcpy(settings.asr_url, "https://openrouter.ai/api/v1/audio/transcriptions");
    assert(transcribe(transcript) == ESP_OK); inspect_openrouter();
    reset("{\"text\":\"Buy milk\"}"); release_samples = 3200;
    strcpy(settings.asr_url, "https://openrouter.ai/api/v1/audio/transcriptions");
    settings.asr_upload = LLM_ASR_UPLOAD_MULTIPART;
    assert(transcribe(transcript) == ESP_OK); inspect_wav(true);
    reset("{\"text\":\"Buy milk\"}"); release_samples = 3200;
    settings.asr_upload = LLM_ASR_UPLOAD_BASE64_JSON;
    settings.asr_language[0] = 0;
    assert(transcribe(transcript) == ESP_OK); inspect_openrouter();
    reset("{\"text\":\"Buy milk\"}"); release_samples = 3200; header_transport_failures = 1;
    assert(transcribe(transcript) == ESP_OK && init_calls == 2 && cleanups == 2);
    reset("{\"text\":\"hallucinated\"}"); silent = true;
    assert(transcribe(transcript) == ESP_ERR_NOT_FOUND && !transcript[0]);
    reset("{\"text\":\"too short\"}"); release_samples = 128;
    assert(transcribe(transcript) == ESP_ERR_NOT_FOUND && !transcript[0]);
    reset("{}"); initial_release = true;
    assert(transcribe(transcript) == ESP_ERR_INVALID_STATE && !start_calls && !cleanups);
    reset("{}"); cancellation = true;
    assert(transcribe(transcript) != ESP_OK && !start_calls && cleanups == 0);
    reset("{}"); init_failure = 1;
    assert(transcribe(transcript) == ESP_ERR_NO_MEM && start_calls == 1 && cleanups == 0);
    reset("{}"); header_failure = 1;
    assert(transcribe(transcript) != ESP_OK && start_calls == 1 && cleanups == 1);
    reset("{}"); open_failure = 1;
    assert(transcribe(transcript) == ESP_FAIL && start_calls == 1 && cleanups == 2);
    reset("{}"); write_failure = 1;
    assert(transcribe(transcript) == ESP_FAIL && start_calls == 1 && cleanups == 2);
    reset("{}"); audio_failure = ESP_FAIL;
    assert(transcribe(transcript) == ESP_FAIL && !start_calls && cleanups == 0);
    reset("{}"); partition_write_failure = true;
    assert(transcribe(transcript) == ESP_FAIL && stop_calls == 1 && cleanups == 0);
    reset("{}"); partition_read_failure = true;
    assert(transcribe(transcript) == ESP_FAIL && stop_calls == 1 && cleanups == 2);
    const char *bad_asr[] = {"{}", "{\"text\":null}", "{\"text\":\"\"}", "{\"text\":\"a\\u0000b\"}", "{\"text\":\"ok\"}garbage"};
    for (size_t i = 0; i < sizeof(bad_asr) / sizeof(*bad_asr); ++i) { reset(bad_asr[i]); assert(transcribe(transcript) != ESP_OK && !transcript[0]); }
    smart_todo_action_t action;
    reset(VALID_CHAT); strcpy(settings.chat_url, "https://openrouter.ai/api/v1/chat/completions");
    assert(interpret(&action) == ESP_OK);
    assert(action.kind == SMART_TODO_ADD && !strcmp(action.title, "Buy milk") && action.delay_seconds == 7200); inspect_chat();
    assert(list.count == 0); /* HTTP layer only proposes; it never mutates storage. */
    reset(BATCH_CHAT);
    assert(interpret(&action) == ESP_OK && action.kind == SMART_TODO_COMPLETE && action.id == 0 &&
           action.id_count == 3 && action.ids[0] == 1 && action.ids[1] == 2 && action.ids[2] == 99);
    reset(VALID_CHAT); strcpy(settings.chat_url, "https://api.siliconflow.cn/v1/chat/completions");
    assert(interpret(&action) == ESP_OK);
    cJSON *siliconflow = cJSON_Parse((const char *)client.upload); assert(siliconflow);
    cJSON *siliconflow_format = cJSON_GetObjectItemCaseSensitive(siliconflow, "response_format");
    assert(cJSON_IsObject(siliconflow_format) &&
           !strcmp(cJSON_GetObjectItemCaseSensitive(siliconflow_format, "type")->valuestring, "json_object") &&
           cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(siliconflow, "enable_thinking")) &&
           cJSON_GetObjectItemCaseSensitive(siliconflow, "temperature")->valuedouble == 0 &&
           !cJSON_GetObjectItemCaseSensitive(siliconflow, "reasoning") &&
           !cJSON_GetObjectItemCaseSensitive(siliconflow, "plugins"));
    cJSON_Delete(siliconflow);
    reset(VALID_CHAT); strcpy(settings.chat_url, "https://api.siliconflow.com/v1/chat/completions");
    assert(interpret(&action) == ESP_OK);
    siliconflow = cJSON_Parse((const char *)client.upload); assert(siliconflow);
    siliconflow_format = cJSON_GetObjectItemCaseSensitive(siliconflow, "response_format");
    assert(cJSON_IsObject(siliconflow_format) &&
           !strcmp(cJSON_GetObjectItemCaseSensitive(siliconflow_format, "type")->valuestring, "json_object") &&
           cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(siliconflow, "enable_thinking")) &&
           cJSON_GetObjectItemCaseSensitive(siliconflow, "temperature")->valuedouble == 0 &&
           !cJSON_GetObjectItemCaseSensitive(siliconflow, "reasoning") &&
           !cJSON_GetObjectItemCaseSensitive(siliconflow, "plugins"));
    cJSON_Delete(siliconflow);
    const char *bad_chat[] = {"{}", "{\"choices\":{\"x\":{\"finish_reason\":\"stop\",\"message\":{\"content\":\"{\\\"action\\\":\\\"noop\\\"}\"}}}}",
        "{\"choices\":[{\"finish_reason\":\"length\",\"message\":{\"content\":\"{\\\"action\\\":\\\"noop\\\"}\"}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":null}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":\"{\\\"action\\\":\\\"delete\\\",\\\"id\\\":-1}\"}}]}",
        "{\"choices\":[]}", "{\"choices\":[null]}", "{\"choices\":[{},{}]}", "{\"choices\":[{\"message\":\"wrong\"}]}"};
    for (size_t i = 0; i < sizeof(bad_chat) / sizeof(*bad_chat); ++i) {
        reset(bad_chat[i]); memset(&action, 0x5a, sizeof(action)); smart_todo_action_t before = action;
        assert(interpret(&action) != ESP_OK && !memcmp(&action, &before, sizeof(action)));
    }
    const int rejected_status[] = {301, 302, 307, 308, 400, 401, 429, 500};
    for (size_t i = 0; i < sizeof(rejected_status) / sizeof(*rejected_status); ++i) {
        reset(VALID_CHAT); response_status = rejected_status[i];
        assert(interpret(&action) != ESP_OK && client.read_offset == 0 && cleanups == 1);
    }
    reset(VALID_CHAT); incomplete = true; assert(interpret(&action) != ESP_OK);
    reset(VALID_CHAT); read_error_at = 11; assert(interpret(&action) != ESP_OK);
    reset(VALID_CHAT); error_after_body = true; assert(interpret(&action) != ESP_OK);
    reset(VALID_CHAT); declared_length = -1; assert(interpret(&action) != ESP_OK);
    reset(VALID_CHAT); declared_length = -1; chunked = true; assert(interpret(&action) == ESP_OK);
    reset(VALID_CHAT); declared_length = 4097; assert(interpret(&action) != ESP_OK && client.read_offset == 0);
    char oversized[5000]; memset(oversized, ' ', sizeof(oversized)); memcpy(oversized, VALID_CHAT, strlen(VALID_CHAT)); oversized[sizeof(oversized) - 1] = 0;
    reset(oversized); declared_length = 0; assert(interpret(&action) != ESP_OK && client.read_offset == 4096);
    reset(VALID_CHAT); declared_length = 0; assert(interpret(&action) == ESP_OK); /* Chunked. */
    reset(VALID_CHAT); cancellation = true; assert(interpret(&action) != ESP_OK);
    reset(VALID_CHAT); settings.enabled = false; assert(interpret(&action) == ESP_ERR_INVALID_ARG && cleanups == 0);
    reset(VALID_CHAT); strcpy(settings.chat_url, "http://example.com/v1/chat/completions");
    assert(interpret(&action) == ESP_ERR_INVALID_ARG && cleanups == 0);
    reset(VALID_CHAT); strcpy(settings.chat_url, "http://192.168.1.20:8000/v1/chat/completions"); settings.api_key[0] = 0;
    assert(interpret(&action) == ESP_OK && !client.authorization[0]);
    for (size_t allocation = 0; allocation < 100; ++allocation) {
        reset(VALID_CHAT); allocation_countdown = allocation;
        cJSON_Hooks hooks = {.malloc_fn = json_allocate, .free_fn = free}; cJSON_InitHooks(&hooks);
        memset(&action, 0x5a, sizeof(action)); smart_todo_action_t before = action;
        esp_err_t error = interpret(&action);
        cJSON_InitHooks(NULL);
        if (error != ESP_OK) assert(!memcmp(&action, &before, sizeof(action)));
        else assert(action.kind == SMART_TODO_ADD && action.delay_seconds == 7200);
    }
    puts("Smart TODO LLM protocol: local recording before network, WAV/multipart/partial IO/silence, TLS policy, bounded complete JSON, cancellation and HTTP errors: PASS");
}
