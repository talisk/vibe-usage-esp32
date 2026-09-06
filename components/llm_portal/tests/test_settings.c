#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "llm_portal.h"
#include "llm_endpoint.h"
#include "nvs_fixture.h"
int main(void) {
    const char *allowed[] = {"https://api.openai.com/v1/chat/completions",
        "https://api.example.com:8443/v1/chat/completions", "https://8.8.8.8/v1/chat/completions",
        "http://192.168.1.10:8000/v1/chat/completions", "http://10.0.0.2/v1/chat/completions",
        "http://172.16.0.1/v1/audio/transcriptions", "http://172.31.255.254:65535/custom/asr"};
    const char *rejected[] = {NULL, "", "http://api.openai.com/v1/chat/completions",
        "https://user:secret@api.openai.com/v1/chat/completions", "https://api.openai.com/v1?key=x",
        "https://api.openai.com/v1#x", "https://api.openai.com/v1\r\nX-Test:x",
        "https://api.openai.com/v1%0d%0aX-Test", "https://api.openai.com/v1%00", "https://api.openai.com/v1%",
        "https://api.openai.com/v1\\path", "https://api.openai.com/v1 path", "ftp://api.openai.com/v1",
        "http://127.0.0.1/v1", "http://169.254.169.254/v1", "http://0.0.0.0/v1", "http://8.8.8.8/v1",
        "http://172.15.0.1/v1", "http://172.32.0.1/v1", "http://192.168.01.10/v1", "http://2130706433/v1",
        "http://0x7f000001/v1", "http://localhost/v1", "http://localhost.localdomain/v1",
        "https://localhost/v1", "https://api.localhost/v1", "https://api.local/v1", "https://api.internal/v1",
        "https://127.0.0.1/v1", "https://169.254.169.254/v1", "https://10.0.0.1/v1",
        "https://[::1]/v1", "http://[fd00::1]/v1", "https://api.openai.com", "https://api.openai.com/",
        "https://api.openai.com:0/v1", "https://api.openai.com:65536/v1", "https://api.openai.com:x/v1",
        "https://-api.openai.com/v1", "https://api..com/v1", "https://api.openai.com./v1",
        "https://1.2.3.999/v1", "https://api.invalid/v1", "https://a.1/v1", "https://api.example.com/\x7f"};
    for (size_t i = 0; i < sizeof(allowed) / sizeof(*allowed); ++i) assert(llm_endpoint_valid(allowed[i]));
    for (size_t i = 0; i < sizeof(rejected) / sizeof(*rejected); ++i) assert(!llm_endpoint_valid(rejected[i]));
    char oversized[300]; memset(oversized, 'a', sizeof(oversized));
    assert(!llm_endpoint_valid(oversized));
    fixture_reset();
    llm_settings_t settings, restored;
    assert(llm_settings_load(NULL) == ESP_ERR_INVALID_ARG);
    assert(llm_settings_load(&settings) == ESP_OK && !settings.enabled);
    assert(llm_settings_valid(&settings));
    assert(settings.asr_upload == LLM_ASR_UPLOAD_AUTO);
    assert(!strcmp(settings.asr_language, "zh"));
    llm_asr_upload_t upload = LLM_ASR_UPLOAD_AUTO;
    assert(!strcmp(llm_asr_upload_name(LLM_ASR_UPLOAD_MULTIPART), "multipart"));
    assert(!strcmp(llm_asr_upload_name(LLM_ASR_UPLOAD_BASE64_JSON), "base64_json"));
    assert(!llm_asr_upload_name((llm_asr_upload_t)99));
    assert(llm_asr_upload_parse("multipart", &upload) && upload == LLM_ASR_UPLOAD_MULTIPART);
    assert(llm_asr_upload_parse("base64_json", &upload) && upload == LLM_ASR_UPLOAD_BASE64_JSON);
    assert(llm_asr_upload_parse("auto", &upload) && upload == LLM_ASR_UPLOAD_AUTO);
    assert(!llm_asr_upload_parse("invalid", &upload) && !llm_asr_upload_parse(NULL, &upload));
    assert(!settings.api_key[0] && !settings.asr_key[0]);
    settings.enabled = true; settings.asr_upload = LLM_ASR_UPLOAD_BASE64_JSON;
    strcpy(settings.asr_language, "en-US");
    strcpy(settings.api_key, "fixture-chat-key"); strcpy(settings.asr_key, "fixture-asr-key");
    assert(llm_settings_save(&settings) == ESP_OK);
    assert(llm_settings_load(&restored) == ESP_OK);
    assert(!memcmp(&settings, &restored, sizeof(settings)));
    settings.asr_language[0] = 0;
    assert(llm_settings_save(&settings) == ESP_OK && llm_settings_load(&restored) == ESP_OK &&
           !restored.asr_language[0]);
    fail_commit = ESP_FAIL;
    assert(llm_settings_save(&settings) == ESP_FAIL);
    assert(llm_settings_clear() == ESP_FAIL);
    fail_commit = 0; fail_write = ESP_FAIL;
    assert(llm_settings_save(&settings) == ESP_FAIL);
    fail_write = 0; fail_read = ESP_FAIL;
    assert(llm_settings_load(&restored) == ESP_FAIL);
    for (size_t i = 0; i < sizeof(restored); ++i) assert(((unsigned char *)&restored)[i] == 0);
    fail_read = ESP_ERR_NVS_TYPE_MISMATCH;
    assert(llm_settings_load(&restored) == ESP_ERR_NVS_TYPE_MISMATCH);
    fail_read = 0; fail_open = ESP_FAIL;
    assert(llm_settings_load(&restored) == ESP_FAIL);
    fail_open = 0;
    fixture_corrupt();
    assert(llm_settings_load(&restored) == ESP_ERR_INVALID_CRC);
    assert(!restored.enabled && !restored.api_key[0]);
    assert(llm_settings_clear() == ESP_OK);
    assert(llm_settings_clear() == ESP_OK);
    assert(llm_settings_load(&restored) == ESP_OK && !restored.enabled);
    memset(settings.chat_url, 'x', sizeof(settings.chat_url));
    assert(!llm_settings_valid(&settings) && llm_settings_save(&settings) == ESP_ERR_INVALID_ARG);
    settings = restored; strcpy(settings.api_key, "bad\r\nheader");
    assert(!llm_settings_valid(&settings));
    settings = restored; memset(settings.asr_key, 'k', sizeof(settings.asr_key));
    assert(!llm_settings_valid(&settings));
    settings = restored; settings.asr_upload = (llm_asr_upload_t)99;
    assert(!llm_settings_valid(&settings));
    settings = restored; strcpy(settings.asr_language, "zh\r\n");
    assert(!llm_settings_valid(&settings));
    settings = restored; strcpy(settings.asr_language, "中文");
    assert(!llm_settings_valid(&settings));
    puts("LLM endpoint policy / NVS round-trip, defaults, corruption and failure: PASS");
}
