#ifndef VIBE_HTTP_POLICY_H_
#define VIBE_HTTP_POLICY_H_

#include <stdbool.h>
#include <stdint.h>

bool vibe_http_content_type_is_json(const char *value);
bool vibe_http_parse_retry_after(const char *value, int64_t now_utc,
                                 uint32_t *delay_seconds);

#endif  // VIBE_HTTP_POLICY_H_
