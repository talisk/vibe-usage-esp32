#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "smart_todo.h"
#include "nvs.h"
static uint8_t disk[SMART_TODO_WIRE_BYTES], pending[SMART_TODO_WIRE_BYTES];
static bool exists, pending_valid;
static esp_err_t fail_open, fail_set, fail_commit;
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) {
    assert(!strcmp(ns, "vibe_todo")); if (fail_open) return fail_open;
    if (mode == NVS_READONLY && !exists) return ESP_ERR_NVS_NOT_FOUND;
    *h = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h == 1); pending_valid = false; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size) {
    assert(h == 1 && !strcmp(key, "list_v1"));
    if (!exists) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < sizeof(disk)) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, disk, sizeof(disk)); *size = sizeof(disk); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t n) {
    assert(h == 1 && !strcmp(key, "list_v1") && n == sizeof(disk));
    if (fail_set) return fail_set;
    memcpy(pending, data, n); pending_valid = true; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h == 1); if (fail_commit) return fail_commit;
    if (pending_valid) { memcpy(disk, pending, sizeof(disk)); exists = true; }
    return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t h) { assert(h == 1); exists = false; return ESP_OK; }
static smart_todo_action_t parse(const char *text) {
    smart_todo_action_t a;
    assert(smart_todo_parse_action(text, &a) == ESP_OK); return a;
}
int main(void) {
    const int64_t now = 1788600000;
    smart_todo_list_t list, copy;
    assert(smart_todo_load(&list) == ESP_OK && list.count == 0 && list.next_id == 1);
    smart_todo_list_t batch;
    smart_todo_init(&batch);
    for (unsigned i = 0; i < 6; ++i) {
        smart_todo_action_t add = parse("{\"action\":\"add\",\"title\":\"batch item\"}");
        add.title[6] = (char)('1' + i);
        assert(smart_todo_apply(&batch, &add, now) == ESP_OK);
    }
    smart_todo_action_t many = parse("{\"action\":\"complete\",\"ids\":[1,3,99,3]}");
    assert(many.id == 0 && many.id_count == 4 && many.ids[0] == 1 && many.ids[2] == 99);
    assert(smart_todo_apply(&batch, &many, now) == ESP_OK);
    assert(batch.items[0].completed && !batch.items[1].completed && batch.items[2].completed &&
           !batch.items[3].completed && !batch.items[4].completed && !batch.items[5].completed);
    many = parse("{\"action\":\"delete\",\"ids\":[2,4,99,4]}");
    assert(smart_todo_apply(&batch, &many, now) == ESP_OK && batch.count == 4 && batch.next_id == 5);
    assert(!strcmp(batch.items[0].title, "batch 1tem") && !strcmp(batch.items[1].title, "batch 3tem") &&
           !strcmp(batch.items[2].title, "batch 5tem") && !strcmp(batch.items[3].title, "batch 6tem"));
    for (unsigned i = 0; i < batch.count; ++i) assert(batch.items[i].id == i + 1U);
    many = parse("{\"action\":\"delete\",\"ids\":[88,99]}"); copy = batch;
    assert(smart_todo_apply(&batch, &many, now) == ESP_ERR_NOT_FOUND && !memcmp(&copy, &batch, sizeof(batch)));
    many = parse("{\"action\":\"complete\",\"ids\":[1,99,1]}"); copy = batch;
    assert(smart_todo_apply(&batch, &many, now) == ESP_ERR_NOT_FOUND && !memcmp(&copy, &batch, sizeof(batch)));
    smart_todo_action_t a = parse("{\"action\":\"add\",\"title\":\"两小时后散步\",\"delay_seconds\":7200}");
    assert(smart_todo_apply(&list, &a, now) == ESP_OK);
    assert(list.items[0].due_utc == now + 7200 && list.items[0].id == 1);
    assert(smart_todo_due(&list, now + 7199) == 0);
    assert(smart_todo_due(&list, now + 7200) == 1);
    assert(smart_todo_save(&list) == ESP_OK);
    assert(smart_todo_load(&copy) == ESP_OK && !memcmp(&list, &copy, sizeof(list)));
    assert(smart_todo_reminded(&copy, 1, now + 7199) != ESP_OK);
    assert(smart_todo_reminded(&copy, 1, now + 8000) == ESP_OK && copy.items[0].due_utc == 0);
    assert(smart_todo_due(&copy, now + 9000) == 0 && !copy.items[0].completed);
    a = parse("{\"action\":\"add\",\"title\":\"喝水\",\"repeat_seconds\":3600}");
    assert(smart_todo_apply(&list, &a, now) == ESP_OK);
    assert(list.items[1].due_utc == now + 3600);
    assert(smart_todo_reminded(&list, 2, now + 3600 * 9 + 7) == ESP_OK);
    assert(list.items[1].due_utc == now + 3600 * 10); /* missed reminders coalesce */
    a = parse("{\"action\":\"complete\",\"id\":2}");
    assert(smart_todo_apply(&list, &a, now) == ESP_OK);
    assert(list.items[1].completed && !list.items[1].due_utc && !list.items[1].repeat_seconds);
    copy = list; assert(smart_todo_apply(&list, &a, now) != ESP_OK && !memcmp(&copy, &list, sizeof(list)));
    a = parse("{\"action\":\"delete\",\"id\":1}");
    assert(smart_todo_apply(&list, &a, now) == ESP_OK && list.count == 1 &&
           list.items[0].id == 1 && list.next_id == 2);
    a.id = 999; copy = list;
    assert(smart_todo_apply(&list, &a, now) != ESP_OK && !memcmp(&copy, &list, sizeof(list)));
    a = parse("{\"action\":\"add\",\"title\":\"next\"}");
    assert(smart_todo_apply(&list, &a, 0) == ESP_OK && list.items[1].id == 2 &&
           list.next_id == 3); /* delete keeps IDs contiguous */
    a.delay_seconds = 1; copy = list;
    assert(smart_todo_apply(&list, &a, 0) != ESP_OK && !memcmp(&copy, &list, sizeof(list)));
    assert(smart_todo_apply(&list, &a, SMART_TODO_TIME_MAX) != ESP_OK);
    a.delay_seconds = 0;
    while (list.count < SMART_TODO_CAPACITY) assert(smart_todo_apply(&list, &a, now) == ESP_OK);
    copy = list;
    assert(smart_todo_apply(&list, &a, now) == ESP_ERR_NO_MEM && !memcmp(&copy, &list, sizeof(list)));
    const char *bad[] = {
        "{\"action\":\"delete\",\"id\":1,\"id\":2}",
        "{\"action\":\"delete\",\"ids\":[1],\"ids\":[2]}",
        "{\"action\":\"delete\",\"id\":1,\"ids\":[2]}",
        "{\"action\":\"delete\",\"ids\":[]}", "{\"action\":\"delete\",\"ids\":1}",
        "{\"action\":\"delete\",\"ids\":[0]}", "{\"action\":\"delete\",\"ids\":[-1]}",
        "{\"action\":\"delete\",\"ids\":[1.5]}", "{\"action\":\"delete\",\"ids\":[\"1\"]}",
        "{\"action\":\"delete\",\"ids\":[true]}", "{\"action\":\"delete\",\"ids\":[4294967295]}",
        "{\"action\":\"delete\",\"ids\":[1,2,3,4,5,6,7,8,9,10,11,12,13]}",
        "{\"action\":\"add\",\"title\":\"x\\u0000evil\"}",
        "{\"action\":\"delete\",\"id\":-1}", "{\"action\":\"delete\",\"id\":1.5}",
        "{\"action\":\"delete\",\"id\":1e40}", "{\"action\":\"delete\",\"id\":true}",
        "{\"action\":\"delete\",\"id\":\"1\"}", "{\"action\":\"delete\",\"id\":4294967295}",
        "{\"action\":\"delete\"}", "{\"action\":\"delete_all\"}",
        "{\"action\":\"add\",\"title\":\" \"}", "{\"action\":\"add\",\"title\":\"x\\n\"}",
        "{\"action\":\"add\",\"title\":\"x\",\"repeat_seconds\":1}",
        "{\"action\":\"add\",\"title\":\"x\",\"repeat_seconds\":31622401}",
        "{\"action\":\"add\",\"title\":\"x\",\"delay_seconds\":-3}",
        "{\"action\":\"delete\",\"id\":2,\"url\":\"x\"}",
        "{\"action\":\"complete\",\"id\":2,\"title\":\"new\"}",
        "{\"action\":\"noop\",\"id\":2}", "[]", "{", "{} trailing",
        "{\"action\":\"add\",\"title\":\"\xc0\x80\"}",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memset(&a, 0x7f, sizeof(a)); smart_todo_action_t prior = a;
        assert(smart_todo_parse_action(bad[i], &a) != ESP_OK && !memcmp(&a, &prior, sizeof(a)));
    }
    assert(!smart_todo_json_safe("[[[[[[[[[[[[[]]]]]]]]]]]]]", 26));
    uint8_t wire[SMART_TODO_WIRE_BYTES];
    assert(smart_todo_encode(&list, wire, sizeof(wire)) == ESP_OK);
    for (unsigned i = 0; i < sizeof(wire); ++i) {
        wire[i] ^= 1; copy = list;
        assert(smart_todo_decode(wire, sizeof(wire), &copy) != ESP_OK && !memcmp(&copy, &list, sizeof(copy)));
        wire[i] ^= 1;
    }
    assert(smart_todo_save(&list) == ESP_OK);
    a = parse("{\"action\":\"delete\",\"id\":2}"); copy = list;
    assert(smart_todo_apply(&copy, &a, now) == ESP_OK);
    fail_set = ESP_FAIL; assert(smart_todo_save(&copy) == ESP_FAIL); fail_set = 0;
    smart_todo_list_t loaded;
    assert(smart_todo_load(&loaded) == ESP_OK && !memcmp(&loaded, &list, sizeof(list)));
    fail_commit = ESP_FAIL; assert(smart_todo_save(&copy) == ESP_FAIL); fail_commit = 0;
    assert(smart_todo_load(&loaded) == ESP_OK && !memcmp(&loaded, &list, sizeof(list)));
    fail_open = ESP_FAIL; assert(smart_todo_load(&loaded) == ESP_FAIL); fail_open = 0;
    disk[24] ^= 1; assert(smart_todo_load(&loaded) != ESP_OK); /* corruption never silently resets */
    assert(smart_todo_clear() == ESP_OK && smart_todo_load(&loaded) == ESP_OK && loaded.count == 0);
    puts("Smart TODO core/storage: PASS (schema, Unicode, IDs, reminders, reboot, corruption, failures)");
}
