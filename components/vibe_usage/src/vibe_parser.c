#include "vibe_parser.h"

#include <limits.h>
#include <string.h>

#include "vibe_date.h"

enum {
    LEX_IDLE = 0,
    LEX_STRING,
    LEX_ESCAPE,
    LEX_UNICODE,
    LEX_NUMBER,
    LEX_LITERAL,
    LEX_LOW_BACKSLASH,
    LEX_LOW_U,
};

enum {
    TOKEN_LEFT_OBJECT = 1,
    TOKEN_RIGHT_OBJECT,
    TOKEN_LEFT_ARRAY,
    TOKEN_RIGHT_ARRAY,
    TOKEN_COLON,
    TOKEN_COMMA,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NULL,
};

enum {
    FRAME_OBJECT = 1,
    FRAME_ARRAY,
};

enum {
    OBJECT_KEY_OR_END = 1,
    OBJECT_COLON,
    OBJECT_VALUE,
    OBJECT_COMMA_OR_END,
    ARRAY_VALUE_OR_END,
    ARRAY_COMMA_OR_END,
    OBJECT_KEY_REQUIRED,
    ARRAY_VALUE_REQUIRED,
};

enum {
    ROLE_IGNORE = 0,
    ROLE_ROOT,
    ROLE_BUCKETS_ARRAY,
    ROLE_BUCKET,
};

static bool is_space(uint8_t byte) {
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

static bool key_is(const vibe_json_frame_t *frame, const char *key) {
    return !frame->key_truncated && strcmp(frame->key, key) == 0;
}

static bool token_has_embedded_nul(const vibe_usage_parser_t *parser) {
    return parser->token_length != 0U &&
           memchr(parser->token, '\0', parser->token_length) != NULL;
}

static void token_reset(vibe_usage_parser_t *parser) {
    parser->token_length = 0;
    parser->token_total_length = 0;
    parser->token_truncated = false;
    parser->token[0] = '\0';
    parser->utf8_remaining = 0;
    parser->utf8_codepoint = 0;
    parser->utf8_minimum = 0;
}

static void token_append(vibe_usage_parser_t *parser, uint8_t byte) {
    ++parser->token_total_length;
    if (parser->token_length + 1U < sizeof(parser->token)) {
        parser->token[parser->token_length++] = (char)byte;
        parser->token[parser->token_length] = '\0';
    } else {
        parser->token_truncated = true;
    }
}

static bool append_codepoint(vibe_usage_parser_t *parser, uint32_t codepoint) {
    if (codepoint <= 0x7fU) {
        token_append(parser, (uint8_t)codepoint);
    } else if (codepoint <= 0x7ffU) {
        token_append(parser, (uint8_t)(0xc0U | (codepoint >> 6U)));
        token_append(parser, (uint8_t)(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
        token_append(parser, (uint8_t)(0xe0U | (codepoint >> 12U)));
        token_append(parser, (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3fU)));
        token_append(parser, (uint8_t)(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0x10ffffU) {
        token_append(parser, (uint8_t)(0xf0U | (codepoint >> 18U)));
        token_append(parser, (uint8_t)(0x80U | ((codepoint >> 12U) & 0x3fU)));
        token_append(parser, (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3fU)));
        token_append(parser, (uint8_t)(0x80U | (codepoint & 0x3fU)));
    } else {
        return false;
    }
    return true;
}

static int hex_value(uint8_t byte) {
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

static bool json_number_valid(const char *text, size_t length) {
    if (length == 0) return false;
    size_t cursor = 0;
    if (text[cursor] == '-') ++cursor;
    if (cursor == length) return false;
    if (text[cursor] == '0') {
        ++cursor;
        if (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            return false;
        }
    } else {
        if (text[cursor] < '1' || text[cursor] > '9') return false;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            ++cursor;
        }
    }
    if (cursor < length && text[cursor] == '.') {
        ++cursor;
        const size_t start = cursor;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            ++cursor;
        }
        if (cursor == start) return false;
    }
    if (cursor < length && (text[cursor] == 'e' || text[cursor] == 'E')) {
        ++cursor;
        if (cursor < length && (text[cursor] == '+' || text[cursor] == '-')) {
            ++cursor;
        }
        const size_t start = cursor;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            ++cursor;
        }
        if (cursor == start) return false;
    }
    return cursor == length;
}

static vibe_error_t parse_u64_token(const vibe_usage_parser_t *parser,
                                    uint64_t *value) {
    if (parser->token_truncated || parser->token_length == 0 ||
        parser->token_length > 32 || value == NULL) {
        return VIBE_ERR_SCHEMA;
    }
    uint64_t result = 0;
    for (size_t i = 0; i < parser->token_length; ++i) {
        const char digit = parser->token[i];
        if (digit < '0' || digit > '9') return VIBE_ERR_SCHEMA;
        const uint64_t numeric = (uint64_t)(digit - '0');
        if (result > (UINT64_MAX - numeric) / 10U) return VIBE_ERR_OVERFLOW;
        result = result * 10U + numeric;
    }
    *value = result;
    return VIBE_OK;
}

static void reset_bucket(vibe_usage_parser_t *parser) {
    parser->bucket_seen_source = false;
    parser->bucket_seen_start = false;
    parser->bucket_seen_total = false;
    parser->bucket_source_other = false;
    parser->bucket_source[0] = '\0';
    parser->bucket_start[0] = '\0';
    parser->bucket_total = 0;
}

static vibe_error_t finish_bucket(vibe_usage_parser_t *parser) {
    if (!parser->bucket_seen_source || !parser->bucket_seen_start ||
        !parser->bucket_seen_total || parser->bucket_source[0] == '\0') {
        return VIBE_ERR_SCHEMA;
    }
    int32_t actual_date;
    vibe_error_t error = vibe_iso8601_to_date(parser->bucket_start,
                                              parser->timezone, &actual_date);
    if (error != VIBE_OK) return error;
    if (actual_date != parser->target_date_key) return VIBE_ERR_OUT_OF_RANGE;
    if (parser->candidate->bucket_count >= parser->max_buckets) {
        return VIBE_ERR_TOO_MANY_BUCKETS;
    }
    return vibe_day_candidate_add(
        parser->candidate,
        parser->bucket_source_other ? VIBE_OTHER_SOURCE_ID
                                    : parser->bucket_source,
        parser->bucket_total);
}

static vibe_error_t push_frame(vibe_usage_parser_t *parser, uint8_t type,
                               uint8_t role) {
    if (parser->depth >= VIBE_PARSER_MAX_DEPTH) return VIBE_ERR_INVALID_JSON;
    vibe_json_frame_t *frame = &parser->frames[parser->depth++];
    memset(frame, 0, sizeof(*frame));
    frame->type = type;
    frame->role = role;
    frame->state = type == FRAME_OBJECT ? OBJECT_KEY_OR_END
                                        : ARRAY_VALUE_OR_END;
    if (role == ROLE_BUCKET) reset_bucket(parser);
    return VIBE_OK;
}

static vibe_error_t close_frame(vibe_usage_parser_t *parser, uint8_t type) {
    if (parser->depth == 0) return VIBE_ERR_INVALID_JSON;
    const vibe_json_frame_t frame = parser->frames[parser->depth - 1U];
    if (frame.type != type) return VIBE_ERR_INVALID_JSON;

    if (frame.role == ROLE_BUCKET) {
        vibe_error_t error = finish_bucket(parser);
        if (error != VIBE_OK) return error;
    } else if (frame.role == ROLE_BUCKETS_ARRAY) {
        parser->seen_buckets = true;
    } else if (frame.role == ROLE_ROOT) {
        if (!parser->seen_buckets || !parser->seen_has_any_data) {
            return VIBE_ERR_SCHEMA;
        }
    }

    --parser->depth;
    if (parser->depth == 0) parser->root_complete = true;
    return VIBE_OK;
}

static vibe_error_t handle_scalar(vibe_usage_parser_t *parser,
                                  vibe_json_frame_t *frame, uint8_t token) {
    if (frame->role == ROLE_ROOT) {
        if (key_is(frame, "buckets")) return VIBE_ERR_SCHEMA;
        if (key_is(frame, "next") || key_is(frame, "nextCursor") ||
            key_is(frame, "cursor") || key_is(frame, "truncated") ||
            key_is(frame, "hasMore") || key_is(frame, "pagination")) {
            return VIBE_ERR_SCHEMA;
        }
        if (key_is(frame, "hasAnyData")) {
            if (parser->seen_has_any_data ||
                (token != TOKEN_TRUE && token != TOKEN_FALSE)) {
                return VIBE_ERR_SCHEMA;
            }
            parser->seen_has_any_data = true;
            parser->root_has_any_data = token == TOKEN_TRUE;
        }
        return VIBE_OK;
    }

    if (frame->role != ROLE_BUCKET) return VIBE_OK;
    if (key_is(frame, "source")) {
        if (parser->bucket_seen_source || token != TOKEN_STRING) {
            return VIBE_ERR_SCHEMA;
        }
        if (token_has_embedded_nul(parser)) return VIBE_ERR_SCHEMA;
        parser->bucket_seen_source = true;
        if (parser->token_total_length == 0) return VIBE_ERR_SCHEMA;
        if (parser->token_truncated ||
            parser->token_total_length >= VIBE_SOURCE_ID_BYTES) {
            parser->bucket_source_other = true;
            strncpy(parser->bucket_source, VIBE_OTHER_SOURCE_ID,
                    sizeof(parser->bucket_source) - 1U);
        } else {
            memcpy(parser->bucket_source, parser->token,
                   parser->token_length + 1U);
        }
    } else if (key_is(frame, "bucketStart")) {
        if (parser->bucket_seen_start || token != TOKEN_STRING ||
            token_has_embedded_nul(parser) ||
            parser->token_truncated || parser->token_total_length == 0 ||
            parser->token_total_length >= sizeof(parser->bucket_start)) {
            return VIBE_ERR_SCHEMA;
        }
        parser->bucket_seen_start = true;
        memcpy(parser->bucket_start, parser->token, parser->token_length + 1U);
    } else if (key_is(frame, "totalTokens")) {
        if (parser->bucket_seen_total || token != TOKEN_NUMBER) {
            return VIBE_ERR_SCHEMA;
        }
        parser->bucket_seen_total = true;
        return parse_u64_token(parser, &parser->bucket_total);
    }
    return VIBE_OK;
}

static vibe_error_t consume_value(vibe_usage_parser_t *parser,
                                  vibe_json_frame_t *parent, uint8_t token) {
    const bool is_container = token == TOKEN_LEFT_OBJECT ||
                              token == TOKEN_LEFT_ARRAY;
    if (!is_container) {
        if (token != TOKEN_STRING && token != TOKEN_NUMBER &&
            token != TOKEN_TRUE && token != TOKEN_FALSE &&
            token != TOKEN_NULL) {
            return VIBE_ERR_INVALID_JSON;
        }
        if (parent->role == ROLE_BUCKETS_ARRAY) return VIBE_ERR_SCHEMA;
        return handle_scalar(parser, parent, token);
    }

    uint8_t role = ROLE_IGNORE;
    if (parent->role == ROLE_ROOT && key_is(parent, "buckets")) {
        if (parser->seen_buckets || token != TOKEN_LEFT_ARRAY) {
            return VIBE_ERR_SCHEMA;
        }
        role = ROLE_BUCKETS_ARRAY;
    } else if (parent->role == ROLE_ROOT && key_is(parent, "hasAnyData")) {
        return VIBE_ERR_SCHEMA;
    } else if (parent->role == ROLE_ROOT &&
               (key_is(parent, "next") || key_is(parent, "nextCursor") ||
                key_is(parent, "cursor") || key_is(parent, "truncated") ||
                key_is(parent, "hasMore") ||
                key_is(parent, "pagination"))) {
        return VIBE_ERR_SCHEMA;
    } else if (parent->role == ROLE_BUCKETS_ARRAY) {
        if (token != TOKEN_LEFT_OBJECT) return VIBE_ERR_SCHEMA;
        role = ROLE_BUCKET;
    } else if (parent->role == ROLE_BUCKET &&
               (key_is(parent, "source") || key_is(parent, "bucketStart") ||
                key_is(parent, "totalTokens"))) {
        return VIBE_ERR_SCHEMA;
    }
    return push_frame(parser,
                      token == TOKEN_LEFT_OBJECT ? FRAME_OBJECT : FRAME_ARRAY,
                      role);
}

static vibe_error_t handle_token(vibe_usage_parser_t *parser, uint8_t token) {
    if (parser->root_complete) return VIBE_ERR_INVALID_JSON;
    if (!parser->root_started) {
        if (token != TOKEN_LEFT_OBJECT) return VIBE_ERR_INVALID_JSON;
        parser->root_started = true;
        return push_frame(parser, FRAME_OBJECT, ROLE_ROOT);
    }
    if (parser->depth == 0) return VIBE_ERR_INVALID_JSON;

    vibe_json_frame_t *frame = &parser->frames[parser->depth - 1U];
    if (frame->type == FRAME_OBJECT) {
        switch (frame->state) {
            case OBJECT_KEY_OR_END:
                if (token == TOKEN_RIGHT_OBJECT) {
                    return close_frame(parser, FRAME_OBJECT);
                }
                __attribute__((fallthrough));
            case OBJECT_KEY_REQUIRED:
                if (token != TOKEN_STRING) return VIBE_ERR_INVALID_JSON;
                frame->key_truncated = parser->token_truncated ||
                                       token_has_embedded_nul(parser) ||
                                       parser->token_total_length >=
                                           sizeof(frame->key);
                if (frame->key_truncated) {
                    frame->key[0] = '\0';
                } else {
                    memcpy(frame->key, parser->token, parser->token_length + 1U);
                }
                frame->state = OBJECT_COLON;
                return VIBE_OK;
            case OBJECT_COLON:
                if (token != TOKEN_COLON) return VIBE_ERR_INVALID_JSON;
                frame->state = OBJECT_VALUE;
                return VIBE_OK;
            case OBJECT_VALUE: {
                frame->state = OBJECT_COMMA_OR_END;
                return consume_value(parser, frame, token);
            }
            case OBJECT_COMMA_OR_END:
                if (token == TOKEN_COMMA) {
                    frame->state = OBJECT_KEY_REQUIRED;
                    return VIBE_OK;
                }
                if (token == TOKEN_RIGHT_OBJECT) {
                    return close_frame(parser, FRAME_OBJECT);
                }
                return VIBE_ERR_INVALID_JSON;
            default:
                return VIBE_ERR_INVALID_JSON;
        }
    }

    switch (frame->state) {
        case ARRAY_VALUE_OR_END:
            if (token == TOKEN_RIGHT_ARRAY) return close_frame(parser, FRAME_ARRAY);
            __attribute__((fallthrough));
        case ARRAY_VALUE_REQUIRED:
            frame->state = ARRAY_COMMA_OR_END;
            return consume_value(parser, frame, token);
        case ARRAY_COMMA_OR_END:
            if (token == TOKEN_COMMA) {
                frame->state = ARRAY_VALUE_REQUIRED;
                return VIBE_OK;
            }
            if (token == TOKEN_RIGHT_ARRAY) return close_frame(parser, FRAME_ARRAY);
            return VIBE_ERR_INVALID_JSON;
        default:
            return VIBE_ERR_INVALID_JSON;
    }
}

static vibe_error_t emit_token(vibe_usage_parser_t *parser, uint8_t token) {
    if (token == TOKEN_NUMBER &&
        (parser->token_truncated || parser->token_length > 32U ||
         !json_number_valid(parser->token, parser->token_length))) {
        return VIBE_ERR_INVALID_JSON;
    }
    return handle_token(parser, token);
}

static bool number_byte(uint8_t byte) {
    return (byte >= '0' && byte <= '9') || byte == '-' || byte == '+' ||
           byte == '.' || byte == 'e' || byte == 'E';
}

static vibe_error_t lex_byte(vibe_usage_parser_t *parser, uint8_t byte,
                             bool *consumed) {
    *consumed = true;
    switch (parser->lex_state) {
        case LEX_IDLE:
            if (is_space(byte)) return VIBE_OK;
            switch (byte) {
                case '{': return emit_token(parser, TOKEN_LEFT_OBJECT);
                case '}': return emit_token(parser, TOKEN_RIGHT_OBJECT);
                case '[': return emit_token(parser, TOKEN_LEFT_ARRAY);
                case ']': return emit_token(parser, TOKEN_RIGHT_ARRAY);
                case ':': return emit_token(parser, TOKEN_COLON);
                case ',': return emit_token(parser, TOKEN_COMMA);
                case '"':
                    token_reset(parser);
                    parser->lex_state = LEX_STRING;
                    return VIBE_OK;
                case 't':
                case 'f':
                case 'n':
                    parser->literal_kind = byte == 't' ? TOKEN_TRUE
                                           : byte == 'f' ? TOKEN_FALSE
                                                         : TOKEN_NULL;
                    parser->literal_position = 1;
                    parser->lex_state = LEX_LITERAL;
                    return VIBE_OK;
                default:
                    if (byte == '-' || (byte >= '0' && byte <= '9')) {
                        token_reset(parser);
                        token_append(parser, byte);
                        parser->lex_state = LEX_NUMBER;
                        return VIBE_OK;
                    }
                    return VIBE_ERR_INVALID_JSON;
            }
        case LEX_STRING:
            if (parser->utf8_remaining != 0) {
                if (byte < 0x80U || byte > 0xbfU) {
                    return VIBE_ERR_INVALID_JSON;
                }
                token_append(parser, byte);
                parser->utf8_codepoint =
                    (parser->utf8_codepoint << 6U) | (byte & 0x3fU);
                if (--parser->utf8_remaining == 0 &&
                    (parser->utf8_codepoint < parser->utf8_minimum ||
                     parser->utf8_codepoint > 0x10ffffU ||
                     (parser->utf8_codepoint >= 0xd800U &&
                      parser->utf8_codepoint <= 0xdfffU))) {
                    return VIBE_ERR_INVALID_JSON;
                }
                return VIBE_OK;
            }
            if (byte == '"') {
                parser->lex_state = LEX_IDLE;
                return emit_token(parser, TOKEN_STRING);
            }
            if (byte == '\\') {
                parser->lex_state = LEX_ESCAPE;
                return VIBE_OK;
            }
            if (byte < 0x20U) return VIBE_ERR_INVALID_JSON;
            if (byte >= 0x80U) {
                if (byte >= 0xc2U && byte <= 0xdfU) {
                    parser->utf8_remaining = 1;
                    parser->utf8_codepoint = byte & 0x1fU;
                    parser->utf8_minimum = 0x80U;
                } else if (byte >= 0xe0U && byte <= 0xefU) {
                    parser->utf8_remaining = 2;
                    parser->utf8_codepoint = byte & 0x0fU;
                    parser->utf8_minimum = 0x800U;
                } else if (byte >= 0xf0U && byte <= 0xf4U) {
                    parser->utf8_remaining = 3;
                    parser->utf8_codepoint = byte & 0x07U;
                    parser->utf8_minimum = 0x10000U;
                } else {
                    return VIBE_ERR_INVALID_JSON;
                }
            }
            token_append(parser, byte);
            return VIBE_OK;
        case LEX_ESCAPE:
            parser->lex_state = LEX_STRING;
            switch (byte) {
                case '"': case '\\': case '/': token_append(parser, byte); return VIBE_OK;
                case 'b': token_append(parser, '\b'); return VIBE_OK;
                case 'f': token_append(parser, '\f'); return VIBE_OK;
                case 'n': token_append(parser, '\n'); return VIBE_OK;
                case 'r': token_append(parser, '\r'); return VIBE_OK;
                case 't': token_append(parser, '\t'); return VIBE_OK;
                case 'u':
                    parser->unicode_digits = 0;
                    parser->unicode_value = 0;
                    parser->lex_state = LEX_UNICODE;
                    return VIBE_OK;
                default: return VIBE_ERR_INVALID_JSON;
            }
        case LEX_UNICODE: {
            const int value = hex_value(byte);
            if (value < 0) return VIBE_ERR_INVALID_JSON;
            parser->unicode_value = (parser->unicode_value << 4U) | (uint32_t)value;
            if (++parser->unicode_digits < 4U) return VIBE_OK;
            const uint32_t code = parser->unicode_value;
            if (parser->pending_high_surrogate != 0) {
                if (code < 0xdc00U || code > 0xdfffU) return VIBE_ERR_INVALID_JSON;
                const uint32_t combined = 0x10000U +
                    (((uint32_t)parser->pending_high_surrogate - 0xd800U) << 10U) +
                    (code - 0xdc00U);
                parser->pending_high_surrogate = 0;
                parser->lex_state = LEX_STRING;
                return append_codepoint(parser, combined) ? VIBE_OK
                                                           : VIBE_ERR_INVALID_JSON;
            }
            if (code >= 0xd800U && code <= 0xdbffU) {
                parser->pending_high_surrogate = (uint16_t)code;
                parser->lex_state = LEX_LOW_BACKSLASH;
                return VIBE_OK;
            }
            if (code >= 0xdc00U && code <= 0xdfffU) return VIBE_ERR_INVALID_JSON;
            parser->lex_state = LEX_STRING;
            return append_codepoint(parser, code) ? VIBE_OK
                                                   : VIBE_ERR_INVALID_JSON;
        }
        case LEX_LOW_BACKSLASH:
            if (byte != '\\') return VIBE_ERR_INVALID_JSON;
            parser->lex_state = LEX_LOW_U;
            return VIBE_OK;
        case LEX_LOW_U:
            if (byte != 'u') return VIBE_ERR_INVALID_JSON;
            parser->unicode_digits = 0;
            parser->unicode_value = 0;
            parser->lex_state = LEX_UNICODE;
            return VIBE_OK;
        case LEX_NUMBER:
            if (number_byte(byte)) {
                token_append(parser, byte);
                return VIBE_OK;
            }
            parser->lex_state = LEX_IDLE;
            *consumed = false;
            return emit_token(parser, TOKEN_NUMBER);
        case LEX_LITERAL: {
            const char *expected = parser->literal_kind == TOKEN_TRUE
                                       ? "true"
                                       : parser->literal_kind == TOKEN_FALSE
                                             ? "false"
                                             : "null";
            if (byte != (uint8_t)expected[parser->literal_position]) {
                return VIBE_ERR_INVALID_JSON;
            }
            ++parser->literal_position;
            if (expected[parser->literal_position] == '\0') {
                parser->lex_state = LEX_IDLE;
                return emit_token(parser, parser->literal_kind);
            }
            return VIBE_OK;
        }
        default:
            return VIBE_ERR_INVALID_JSON;
    }
}

void vibe_usage_parser_init(vibe_usage_parser_t *parser,
                            vibe_day_candidate_t *candidate,
                            int32_t target_date_key,
                            vibe_timezone_t timezone) {
    if (parser == NULL) return;
    memset(parser, 0, sizeof(*parser));
    parser->candidate = candidate;
    parser->target_date_key = target_date_key;
    parser->timezone = timezone;
    parser->max_body_bytes = VIBE_PARSER_DEFAULT_MAX_BODY;
    parser->max_buckets = VIBE_PARSER_DEFAULT_MAX_BUCKETS;
    if (candidate == NULL || !vibe_date_is_valid(target_date_key) ||
        (timezone != VIBE_TZ_ASIA_SHANGHAI && timezone != VIBE_TZ_UTC)) {
        parser->error = VIBE_ERR_INVALID_ARGUMENT;
        return;
    }
    vibe_day_candidate_init(candidate, target_date_key);
}

void vibe_usage_parser_set_limits(vibe_usage_parser_t *parser,
                                  size_t max_body_bytes,
                                  uint32_t max_buckets) {
    if (parser == NULL || max_body_bytes == 0 || max_buckets == 0) return;
    parser->max_body_bytes = max_body_bytes;
    parser->max_buckets = max_buckets;
}

vibe_error_t vibe_usage_parser_feed(vibe_usage_parser_t *parser,
                                    const uint8_t *data, size_t size) {
    if (parser == NULL || (data == NULL && size != 0)) {
        return VIBE_ERR_INVALID_ARGUMENT;
    }
    if (parser->error != VIBE_OK) return parser->error;
    if (parser->body_bytes > parser->max_body_bytes ||
        size > parser->max_body_bytes - parser->body_bytes) {
        parser->error = VIBE_ERR_BODY_TOO_LARGE;
        return parser->error;
    }
    parser->body_bytes += size;
    for (size_t i = 0; i < size;) {
        bool consumed;
        parser->error = lex_byte(parser, data[i], &consumed);
        if (parser->error != VIBE_OK) return parser->error;
        if (consumed) ++i;
    }
    return VIBE_OK;
}

vibe_error_t vibe_usage_parser_finish(vibe_usage_parser_t *parser) {
    if (parser == NULL) return VIBE_ERR_INVALID_ARGUMENT;
    if (parser->error != VIBE_OK) return parser->error;
    if (parser->lex_state == LEX_NUMBER) {
        parser->lex_state = LEX_IDLE;
        parser->error = emit_token(parser, TOKEN_NUMBER);
    } else if (parser->lex_state != LEX_IDLE) {
        parser->error = VIBE_ERR_INVALID_JSON;
    }
    if (parser->error == VIBE_OK &&
        (!parser->root_started || !parser->root_complete || parser->depth != 0 ||
         !parser->seen_buckets || !parser->seen_has_any_data)) {
        parser->error = VIBE_ERR_INVALID_JSON;
    }
    if (parser->error == VIBE_OK) {
        parser->candidate->has_any_data = parser->root_has_any_data;
    }
    return parser->error;
}
