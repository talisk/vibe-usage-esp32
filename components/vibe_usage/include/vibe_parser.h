#ifndef VIBE_PARSER_H_
#define VIBE_PARSER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vibe_aggregate.h"
#include "vibe_usage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIBE_PARSER_MAX_DEPTH 16U
#define VIBE_PARSER_TOKEN_BYTES 1025U
#define VIBE_PARSER_DEFAULT_MAX_BODY (2U * 1024U * 1024U)
#define VIBE_PARSER_DEFAULT_MAX_BUCKETS 20000U

typedef struct {
    uint8_t type;
    uint8_t state;
    uint8_t role;
    bool key_truncated;
    char key[40];
} vibe_json_frame_t;

typedef struct {
    vibe_day_candidate_t *candidate;
    int32_t target_date_key;
    vibe_timezone_t timezone;
    size_t max_body_bytes;
    uint32_t max_buckets;
    size_t body_bytes;
    vibe_error_t error;

    uint8_t lex_state;
    uint8_t literal_kind;
    uint8_t literal_position;
    uint8_t unicode_digits;
    uint32_t unicode_value;
    uint16_t pending_high_surrogate;
    uint8_t utf8_remaining;
    uint32_t utf8_codepoint;
    uint32_t utf8_minimum;
    char token[VIBE_PARSER_TOKEN_BYTES];
    size_t token_length;
    size_t token_total_length;
    bool token_truncated;

    vibe_json_frame_t frames[VIBE_PARSER_MAX_DEPTH];
    uint8_t depth;
    bool root_started;
    bool root_complete;
    bool seen_buckets;
    bool seen_has_any_data;
    bool root_has_any_data;

    bool bucket_seen_source;
    bool bucket_seen_start;
    bool bucket_seen_total;
    bool bucket_source_other;
    char bucket_source[VIBE_SOURCE_ID_BYTES];
    char bucket_start[65];
    uint64_t bucket_total;
} vibe_usage_parser_t;

void vibe_usage_parser_init(vibe_usage_parser_t *parser,
                            vibe_day_candidate_t *candidate,
                            int32_t target_date_key,
                            vibe_timezone_t timezone);
void vibe_usage_parser_set_limits(vibe_usage_parser_t *parser,
                                  size_t max_body_bytes,
                                  uint32_t max_buckets);
vibe_error_t vibe_usage_parser_feed(vibe_usage_parser_t *parser,
                                    const uint8_t *data, size_t size);
vibe_error_t vibe_usage_parser_finish(vibe_usage_parser_t *parser);

#ifdef __cplusplus
}
#endif

#endif  // VIBE_PARSER_H_
