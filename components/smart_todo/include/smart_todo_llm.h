#ifndef SMART_TODO_LLM_H_
#define SMART_TODO_LLM_H_
#include "smart_todo.h"
#include "llm_portal.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SMART_TODO_RECORD_SECONDS 8U
#define SMART_TODO_TRANSCRIPT_BYTES 513U
typedef struct {
    bool (*held)(void *context);
    bool (*cancelled)(void *context);
    void (*recording)(bool active, void *context);
    void *context;
} smart_todo_voice_callbacks_t;
esp_err_t smart_todo_transcribe(const llm_settings_t *settings,
    const smart_todo_voice_callbacks_t *callbacks, char *text, size_t capacity,
    int *http_status);
esp_err_t smart_todo_interpret(const llm_settings_t *settings,
    const smart_todo_list_t *list, const char *text,
    const smart_todo_voice_callbacks_t *callbacks,
    smart_todo_action_t *action, int *http_status);
#ifdef __cplusplus
}
#endif
#endif
