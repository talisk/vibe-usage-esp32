#include "smart_todo.h"
#include <stdlib.h>
#include "nvs.h"
esp_err_t smart_todo_load(smart_todo_list_t *list) {
    if (!list) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("vibe_todo", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { smart_todo_init(list); return ESP_OK; }
    if (err != ESP_OK) return err;
    uint8_t *wire = malloc(SMART_TODO_WIRE_BYTES);
    if (!wire) { nvs_close(handle); return ESP_ERR_NO_MEM; }
    size_t size = SMART_TODO_WIRE_BYTES;
    err = nvs_get_blob(handle, "list_v1", wire, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) { smart_todo_init(list); err = ESP_OK; }
    else if (err == ESP_OK) err = smart_todo_decode(wire, size, list);
    free(wire); nvs_close(handle); return err;
}
esp_err_t smart_todo_save(const smart_todo_list_t *list) {
    uint8_t *wire = malloc(SMART_TODO_WIRE_BYTES);
    if (!wire) return ESP_ERR_NO_MEM;
    esp_err_t err = smart_todo_encode(list, wire, SMART_TODO_WIRE_BYTES);
    if (err == ESP_OK) {
        nvs_handle_t handle;
        err = nvs_open("vibe_todo", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_blob(handle, "list_v1", wire, SMART_TODO_WIRE_BYTES);
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
    }
    free(wire); return err;
}
esp_err_t smart_todo_clear(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("vibe_todo", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle); return err;
}
