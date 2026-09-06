#ifndef LLM_PORTAL_H_
#define LLM_PORTAL_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LLM_ASR_UPLOAD_AUTO = 0,
    LLM_ASR_UPLOAD_MULTIPART = 1,
    LLM_ASR_UPLOAD_BASE64_JSON = 2,
} llm_asr_upload_t;

typedef struct {
    bool enabled;
    llm_asr_upload_t asr_upload;
    char chat_url[256];
    char chat_model[64];
    char api_key[256];
    char asr_url[256];
    char asr_model[64];
    /* Empty asks the provider to auto-detect; otherwise an ASCII language tag. */
    char asr_language[16];
    char asr_key[256];
} llm_settings_t;

/* An absent namespace/key loads disabled OpenAI defaults without writing NVS.
 * Corrupt data and storage errors propagate; caller must not use failed loads.
 * Settings include credentials: keep them out of display state and logs. */
esp_err_t llm_settings_load(llm_settings_t *settings);
esp_err_t llm_settings_save(const llm_settings_t *settings);
esp_err_t llm_settings_clear(void);
bool llm_settings_valid(const llm_settings_t *settings);
const char *llm_asr_upload_name(llm_asr_upload_t upload);
bool llm_asr_upload_parse(const char *name, llm_asr_upload_t *upload);

/* Lifecycle calls belong to app_controller. Start requires a station IP and
 * the Wi-Fi setup server to have stopped. Stop before Wi-Fi setup/shutdown. */
esp_err_t llm_portal_start(void);
void llm_portal_stop(void);
const char *llm_portal_url(void);
bool llm_portal_is_running(void);

#ifdef __cplusplus
}
#endif
#endif
