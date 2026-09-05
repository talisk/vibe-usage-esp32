#ifndef VIBE_DEVICE_FLOW_POLICY_H
#define VIBE_DEVICE_FLOW_POLICY_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    VIBE_DEVICE_POLL_SHAPE_INVALID = 0,
    VIBE_DEVICE_POLL_SHAPE_ALREADY_DELIVERED,
    VIBE_DEVICE_POLL_SHAPE_SUCCESS,
    VIBE_DEVICE_POLL_SHAPE_ERROR,
} vibe_device_poll_shape_t;

vibe_device_poll_shape_t vibe_device_poll_classify(
    int http_status, unsigned api_key_count, bool api_key_is_valid_string,
    bool api_key_is_null, unsigned api_url_count, bool api_url_is_string,
    bool api_url_is_null, unsigned error_count, bool error_is_string,
    bool error_is_null);

bool vibe_device_flow_url_is_trusted(const char *url,
                                     const char *trusted_origin);
bool vibe_device_flow_api_key_is_valid(const char *api_key,
                                       size_t capacity);

#endif
