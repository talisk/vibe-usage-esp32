#ifndef VIBE_JSON_SAFETY_H
#define VIBE_JSON_SAFETY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * cJSON exposes decoded strings through NUL-terminated C strings. Reject any
 * response body that could decode an embedded NUL before handing it to cJSON,
 * so a field cannot be accepted under a silently truncated value.
 *
 * This is a safety preflight, not a JSON validator. Syntax validation remains
 * the parser's responsibility.
 */
bool vibe_json_text_is_cstring_safe(const uint8_t *data, size_t size);

#endif
