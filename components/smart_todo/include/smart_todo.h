#ifndef SMART_TODO_H_
#define SMART_TODO_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SMART_TODO_CAPACITY 12U
#define SMART_TODO_TITLE_BYTES 97U
#define SMART_TODO_MAX_INTERVAL (366U * 86400U)
#define SMART_TODO_TIME_MIN 1704067200LL
#define SMART_TODO_TIME_MAX 4102444800LL
#define SMART_TODO_WIRE_BYTES (24U + SMART_TODO_CAPACITY * 120U + 4U)
typedef struct {
    uint32_t id;
    bool completed;
    char title[SMART_TODO_TITLE_BYTES];
    int64_t due_utc;
    uint32_t repeat_seconds;
} smart_todo_item_t;
typedef struct {
    uint32_t revision;
    uint32_t next_id;
    uint8_t count;
    smart_todo_item_t items[SMART_TODO_CAPACITY];
} smart_todo_list_t;
typedef enum { SMART_TODO_ADD, SMART_TODO_COMPLETE, SMART_TODO_DELETE,
               SMART_TODO_NOOP } smart_todo_action_kind_t;
typedef struct {
    smart_todo_action_kind_t kind;
    uint32_t id;
    uint8_t id_count;
    uint32_t ids[SMART_TODO_CAPACITY];
    char title[SMART_TODO_TITLE_BYTES];
    uint32_t delay_seconds;
    uint32_t repeat_seconds;
} smart_todo_action_t;
void smart_todo_init(smart_todo_list_t *list);
bool smart_todo_valid(const smart_todo_list_t *list);
esp_err_t smart_todo_parse_action(const char *json, smart_todo_action_t *action);
esp_err_t smart_todo_apply(smart_todo_list_t *list, const smart_todo_action_t *action,
                           int64_t now_utc);
uint32_t smart_todo_due(const smart_todo_list_t *list, int64_t now_utc);
esp_err_t smart_todo_reminded(smart_todo_list_t *list, uint32_t id, int64_t now_utc);
esp_err_t smart_todo_encode(const smart_todo_list_t *list, uint8_t *out, size_t size);
esp_err_t smart_todo_decode(const uint8_t *data, size_t size, smart_todo_list_t *list);
esp_err_t smart_todo_load(smart_todo_list_t *list);
esp_err_t smart_todo_save(const smart_todo_list_t *list);
esp_err_t smart_todo_clear(void);
/* Reject embedded NUL, escaped NUL and excessive JSON depth before cJSON. */
bool smart_todo_json_safe(const char *json, size_t length);
#ifdef __cplusplus
}
#endif
#endif
