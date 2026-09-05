#include "vibe_json_safety.h"

bool vibe_json_text_is_cstring_safe(const uint8_t *data, size_t size) {
    if (data == NULL) return size == 0U;

    bool in_string = false;
    bool escaped = false;
    for (size_t index = 0; index < size; ++index) {
        const uint8_t byte = data[index];
        if (byte == 0U) return false;

        if (!in_string) {
            if (byte == '"') in_string = true;
            continue;
        }

        if (escaped) {
            if (byte == 'u' && index + 4U < size &&
                data[index + 1U] == '0' && data[index + 2U] == '0' &&
                data[index + 3U] == '0' && data[index + 4U] == '0') {
                return false;
            }
            escaped = false;
        } else if (byte == '\\') {
            escaped = true;
        } else if (byte == '"') {
            in_string = false;
        }
    }
    return true;
}
