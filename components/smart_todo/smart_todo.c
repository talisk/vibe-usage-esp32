#include "smart_todo.h"
#include <math.h>
#include <string.h>
#include "cJSON.h"

static bool text_valid(const char *s, size_t max) {
    if (!s || !s[0]) return false;
    size_t n = 0;
    while (n < max && s[n]) ++n;
    if (n == max) return false;
    bool visible = false;
    for (size_t i = 0; i < n;) {
        uint8_t c = (uint8_t)s[i++];
        if (c < 0x20 || c == 0x7f) return false;
        if (c != ' ') visible = true;
        if (c < 0x80) continue;
        uint32_t cp; size_t extra;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; extra = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; extra = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; extra = 3; }
        else return false;
        if (extra > n - i) return false;
        for (size_t j = 0; j < extra; ++j) {
            c = (uint8_t)s[i++];
            if ((c & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (c & 63);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return visible;
}

bool smart_todo_json_safe(const char *json, size_t length) {
    if (!json || length == 0 || length > 8192) return false;
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (size_t i = 0; i < length; ++i) {
        char c = json[i];
        if (!c) return false;
        if (quoted) {
            if (escaped) {
                if (c == 'u' && i + 4 < length &&
                    memcmp(json + i + 1, "0000", 4) == 0) return false;
                escaped = false;
            } else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 12) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    return !quoted && depth == 0;
}

void smart_todo_init(smart_todo_list_t *list) {
    if (!list) return;
    memset(list, 0, sizeof(*list)); list->next_id = 1;
}

bool smart_todo_valid(const smart_todo_list_t *list) {
    if (!list || list->count > SMART_TODO_CAPACITY || !list->next_id) return false;
    for (unsigned i = 0; i < list->count; ++i) {
        const smart_todo_item_t *a = &list->items[i];
        if (!a->id || a->id >= list->next_id || !text_valid(a->title, sizeof(a->title)) ||
            a->repeat_seconds > SMART_TODO_MAX_INTERVAL ||
            (a->repeat_seconds && a->repeat_seconds < 60) ||
            (a->due_utc && (a->due_utc < SMART_TODO_TIME_MIN || a->due_utc > SMART_TODO_TIME_MAX)) ||
            (a->completed && (a->due_utc || a->repeat_seconds)) ||
            (a->repeat_seconds && !a->due_utc)) return false;
        for (unsigned j = 0; j < i; ++j) if (a->id == list->items[j].id) return false;
    }
    return true;
}

static bool uint_field(const cJSON *root, const char *key, uint32_t max, uint32_t *out) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!v) { *out = 0; return true; }
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||
        v->valuedouble > max || floor(v->valuedouble) != v->valuedouble) return false;
    *out = (uint32_t)v->valuedouble; return true;
}

static bool ids_field(const cJSON *root, smart_todo_action_t *action) {
    const cJSON *ids = cJSON_GetObjectItemCaseSensitive(root, "ids");
    if (!ids) return true;
    if (!cJSON_IsArray(ids)) return false;
    int count = cJSON_GetArraySize(ids);
    if (count <= 0 || count > (int)SMART_TODO_CAPACITY) return false;
    for (int i = 0; i < count; ++i) {
        const cJSON *id = cJSON_GetArrayItem(ids, i);
        if (!cJSON_IsNumber(id) || !isfinite(id->valuedouble) || id->valuedouble <= 0 ||
            id->valuedouble > UINT32_MAX - 1U || floor(id->valuedouble) != id->valuedouble) return false;
        action->ids[i] = (uint32_t)id->valuedouble;
    }
    action->id_count = (uint8_t)count;
    return true;
}

esp_err_t smart_todo_parse_action(const char *json, smart_todo_action_t *action) {
    if (!json || !action || !smart_todo_json_safe(json, strlen(json))) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    esp_err_t result = ESP_ERR_INVALID_RESPONSE;
    smart_todo_action_t candidate = {0};
    if (!cJSON_IsObject(root)) goto done;
    for (const cJSON *a = root->child; a; a = a->next) {
        if (!a->string || (strcmp(a->string, "action") && strcmp(a->string, "id") && strcmp(a->string, "ids") &&
            strcmp(a->string, "title") && strcmp(a->string, "delay_seconds") &&
            strcmp(a->string, "repeat_seconds"))) goto done;
        for (const cJSON *b = a->next; b; b = b->next)
            if (b->string && !strcmp(a->string, b->string)) goto done;
    }
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (!cJSON_IsString(kind)) goto done;
    if (!strcmp(kind->valuestring, "add")) candidate.kind = SMART_TODO_ADD;
    else if (!strcmp(kind->valuestring, "complete")) candidate.kind = SMART_TODO_COMPLETE;
    else if (!strcmp(kind->valuestring, "delete")) candidate.kind = SMART_TODO_DELETE;
    else if (!strcmp(kind->valuestring, "noop")) candidate.kind = SMART_TODO_NOOP;
    else goto done;
    if (!uint_field(root, "id", UINT32_MAX - 1U, &candidate.id) || !ids_field(root, &candidate) ||
        !uint_field(root, "delay_seconds", SMART_TODO_MAX_INTERVAL, &candidate.delay_seconds) ||
        !uint_field(root, "repeat_seconds", SMART_TODO_MAX_INTERVAL, &candidate.repeat_seconds) ||
        (candidate.repeat_seconds && candidate.repeat_seconds < 60)) goto done;
    const cJSON *title = cJSON_GetObjectItemCaseSensitive(root, "title");
    if (title) {
        if (!cJSON_IsString(title) || strlen(title->valuestring) >= sizeof(candidate.title)) goto done;
        strcpy(candidate.title, title->valuestring);
    }
    if (candidate.kind == SMART_TODO_ADD) {
        if (candidate.id || candidate.id_count || !text_valid(candidate.title, sizeof(candidate.title))) goto done;
    } else {
        if (candidate.delay_seconds || candidate.repeat_seconds || candidate.title[0]) goto done;
        if (candidate.id && candidate.id_count) goto done;
        bool has_ids = candidate.id || candidate.id_count;
        if ((candidate.kind == SMART_TODO_NOOP) == has_ids) goto done;
    }
    *action = candidate; result = ESP_OK;
done:
    cJSON_Delete(root); return result;
}

static bool action_ids_valid(const smart_todo_action_t *action) {
    if (action->id && action->id_count) return false;
    if (action->id_count > SMART_TODO_CAPACITY) return false;
    if (action->id) return action->id < UINT32_MAX;
    if (!action->id_count) return false;
    for (unsigned i = 0; i < action->id_count; ++i)
        if (!action->ids[i] || action->ids[i] == UINT32_MAX) return false;
    return true;
}

static bool action_requests_id(const smart_todo_action_t *action, uint32_t id) {
    if (!action->id_count) return action->id == id;
    for (unsigned i = 0; i < action->id_count; ++i)
        if (action->ids[i] == id) return true;
    return false;
}

esp_err_t smart_todo_apply(smart_todo_list_t *list, const smart_todo_action_t *action, int64_t now) {
    if (!smart_todo_valid(list) || !action) return ESP_ERR_INVALID_ARG;
    if (action->kind == SMART_TODO_NOOP) return ESP_ERR_NOT_FOUND;
    if (list->revision == UINT32_MAX) return ESP_ERR_INVALID_STATE;
    if (action->kind == SMART_TODO_ADD) {
        if (list->count == SMART_TODO_CAPACITY || list->next_id == UINT32_MAX) return ESP_ERR_NO_MEM;
        if (action->id || action->id_count || !text_valid(action->title, sizeof(action->title)) ||
            action->delay_seconds > SMART_TODO_MAX_INTERVAL ||
            action->repeat_seconds > SMART_TODO_MAX_INTERVAL ||
            (action->repeat_seconds && action->repeat_seconds < 60)) return ESP_ERR_INVALID_ARG;
        uint32_t delay = action->delay_seconds ? action->delay_seconds : action->repeat_seconds;
        if (delay && (now < SMART_TODO_TIME_MIN || now > SMART_TODO_TIME_MAX - delay)) return ESP_ERR_INVALID_STATE;
        smart_todo_item_t *item = &list->items[list->count++];
        memset(item, 0, sizeof(*item)); item->id = list->next_id++;
        strcpy(item->title, action->title);
        item->due_utc = delay ? now + delay : 0;
        item->repeat_seconds = action->repeat_seconds;
    } else {
        if ((action->kind != SMART_TODO_COMPLETE && action->kind != SMART_TODO_DELETE) ||
            action->delay_seconds || action->repeat_seconds || action->title[0] ||
            !action_ids_valid(action)) return ESP_ERR_INVALID_ARG;
        if (action->kind == SMART_TODO_COMPLETE) {
            bool changed = false;
            for (unsigned i = 0; i < list->count; ++i) {
                smart_todo_item_t *item = &list->items[i];
                if (!action_requests_id(action, item->id) || item->completed) continue;
                item->completed = true; item->due_utc = 0; item->repeat_seconds = 0; changed = true;
            }
            if (!changed) return ESP_ERR_NOT_FOUND;
        } else {
            unsigned write = 0, old_count = list->count;
            for (unsigned read = 0; read < old_count; ++read) {
                if (action_requests_id(action, list->items[read].id)) continue;
                if (write != read) list->items[write] = list->items[read];
                ++write;
            }
            if (write == old_count) return ESP_ERR_NOT_FOUND;
            memset(&list->items[write], 0, (old_count - write) * sizeof(list->items[0]));
            list->count = (uint8_t)write;
            for (unsigned j = 0; j < list->count; ++j) list->items[j].id = j + 1U;
            list->next_id = list->count + 1U;
        }
    }
    ++list->revision; return ESP_OK;
}

uint32_t smart_todo_due(const smart_todo_list_t *list, int64_t now) {
    if (!smart_todo_valid(list) || now < SMART_TODO_TIME_MIN || now > SMART_TODO_TIME_MAX) return 0;
    const smart_todo_item_t *first = NULL;
    for (unsigned i = 0; i < list->count; ++i) {
        const smart_todo_item_t *a = &list->items[i];
        if (!a->completed && a->due_utc && a->due_utc <= now && (!first || a->due_utc < first->due_utc)) first = a;
    }
    return first ? first->id : 0;
}

esp_err_t smart_todo_reminded(smart_todo_list_t *list, uint32_t id, int64_t now) {
    if (!smart_todo_valid(list) || now < SMART_TODO_TIME_MIN || now > SMART_TODO_TIME_MAX ||
        list->revision == UINT32_MAX) return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < list->count; ++i) {
        smart_todo_item_t *a = &list->items[i];
        if (a->id != id) continue;
        if (a->completed || !a->due_utc || a->due_utc > now) return ESP_ERR_INVALID_STATE;
        if (a->repeat_seconds) {
            int64_t steps = (now - a->due_utc) / a->repeat_seconds + 1;
            int64_t next = a->due_utc + steps * a->repeat_seconds;
            if (next > SMART_TODO_TIME_MAX) return ESP_ERR_INVALID_STATE;
            a->due_utc = next;
        } else a->due_utc = 0;
        ++list->revision; return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static void put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8)); }
static uint32_t get32(const uint8_t *p) { uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (i * 8); return v; }
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t v = UINT32_MAX;
    while (n--) { v ^= *p++; for (unsigned i = 0; i < 8; ++i) v = (v >> 1) ^ (0xedb88320U & (0U - (v & 1U))); }
    return ~v;
}
esp_err_t smart_todo_encode(const smart_todo_list_t *list, uint8_t *out, size_t size) {
    if (!smart_todo_valid(list) || !out || size != SMART_TODO_WIRE_BYTES) return ESP_ERR_INVALID_ARG;
    memset(out, 0, size); memcpy(out, "VTOD", 4); put32(out + 4, 1);
    put32(out + 8, list->revision); put32(out + 12, list->next_id); out[16] = list->count;
    for (unsigned i = 0; i < list->count; ++i) {
        const smart_todo_item_t *a = &list->items[i]; uint8_t *p = out + 24 + i * 120;
        put32(p, a->id); p[4] = a->completed; put32(p + 8, (uint32_t)a->due_utc);
        put32(p + 12, a->repeat_seconds); memcpy(p + 16, a->title, strlen(a->title));
    }
    put32(out + size - 4, crc32(out, size - 4)); return ESP_OK;
}
esp_err_t smart_todo_decode(const uint8_t *data, size_t size, smart_todo_list_t *list) {
    if (!data || !list || size != SMART_TODO_WIRE_BYTES || memcmp(data, "VTOD", 4) ||
        get32(data + 4) != 1 || get32(data + size - 4) != crc32(data, size - 4) ||
        data[16] > SMART_TODO_CAPACITY) return ESP_ERR_INVALID_RESPONSE;
    smart_todo_list_t candidate; smart_todo_init(&candidate);
    candidate.revision = get32(data + 8); candidate.next_id = get32(data + 12); candidate.count = data[16];
    for (unsigned i = 0; i < candidate.count; ++i) {
        const uint8_t *p = data + 24 + i * 120; smart_todo_item_t *a = &candidate.items[i];
        if (p[4] > 1 || !memchr(p + 16, 0, SMART_TODO_TITLE_BYTES)) return ESP_ERR_INVALID_RESPONSE;
        a->id = get32(p); a->completed = p[4]; a->due_utc = get32(p + 8); a->repeat_seconds = get32(p + 12);
        memcpy(a->title, p + 16, SMART_TODO_TITLE_BYTES);
    }
    if (!smart_todo_valid(&candidate)) return ESP_ERR_INVALID_RESPONSE;
    *list = candidate; return ESP_OK;
}
