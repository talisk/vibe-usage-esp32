#include "vibe_device_flow_policy.h"

#include <stddef.h>
#include <string.h>

vibe_device_poll_shape_t vibe_device_poll_classify(
    int http_status, unsigned api_key_count, bool api_key_is_valid_string,
    bool api_key_is_null, unsigned api_url_count, bool api_url_is_string,
    bool api_url_is_null, unsigned error_count, bool error_is_string,
    bool error_is_null) {
    const bool key_absent = api_key_count == 0U ||
                            (api_key_count == 1U && api_key_is_null);
    const bool url_absent = api_url_count == 0U ||
                            (api_url_count == 1U && api_url_is_null);
    const bool error_absent = error_count == 0U ||
                              (error_count == 1U && error_is_null);
    if (http_status == 410) {
        return key_absent && url_absent &&
                       (error_absent ||
                        (error_count == 1U && error_is_string))
                   ? VIBE_DEVICE_POLL_SHAPE_ALREADY_DELIVERED
                   : VIBE_DEVICE_POLL_SHAPE_INVALID;
    }
    if (http_status != 200) return VIBE_DEVICE_POLL_SHAPE_INVALID;

    if (api_key_count == 1U && api_key_is_valid_string &&
        error_absent &&
        (url_absent ||
         (api_url_count == 1U && api_url_is_string))) {
        return VIBE_DEVICE_POLL_SHAPE_SUCCESS;
    }
    if (key_absent && url_absent && error_count == 1U && error_is_string) {
        return VIBE_DEVICE_POLL_SHAPE_ERROR;
    }
    return VIBE_DEVICE_POLL_SHAPE_INVALID;
}

bool vibe_device_flow_url_is_trusted(const char *url,
                                     const char *trusted_origin) {
    if (url == NULL || trusted_origin == NULL) return false;
    const size_t origin_length = strlen(trusted_origin);
    if (origin_length == 0U || strncmp(url, trusted_origin, origin_length) != 0 ||
        url[origin_length] != '/') {
        return false;
    }

    const unsigned char *cursor =
        (const unsigned char *)url + origin_length;
    for (; *cursor != '\0'; ++cursor) {
        if (*cursor <= 0x20U || *cursor == 0x7fU || *cursor == '\\' ||
            *cursor == '@' || *cursor == '#') {
            return false;
        }
    }
    return true;
}

bool vibe_device_flow_api_key_is_valid(const char *api_key,
                                       size_t capacity) {
    if (api_key == NULL || capacity <= 5U) return false;
    size_t length = 0;
    while (length < capacity && api_key[length] != '\0') ++length;
    if (length <= 4U || length >= capacity ||
        strncmp(api_key, "vbu_", 4U) != 0) {
        return false;
    }
    for (size_t index = 4U; index < length; ++index) {
        const unsigned char byte = (unsigned char)api_key[index];
        if (!((byte >= 'a' && byte <= 'z') ||
              (byte >= 'A' && byte <= 'Z') ||
              (byte >= '0' && byte <= '9') || byte == '_' || byte == '-')) {
            return false;
        }
    }
    return true;
}
