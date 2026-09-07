#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epd_refresh_policy.h"
#include "vibe_aggregate.h"
#include "vibe_cache.h"
#include "vibe_date.h"
#include "vibe_device_flow_policy.h"
#include "vibe_http_policy.h"
#include "vibe_json_safety.h"
#include "vibe_parser.h"
#include "vibe_state.h"
#include "vibe_list_navigation.h"

static void expect(vibe_error_t actual, vibe_error_t expected,
                   const char *context) {
    if (actual != expected) {
        fprintf(stderr, "%s: expected %s, got %s\n", context,
                vibe_error_name(expected), vibe_error_name(actual));
        abort();
    }
}

static void refresh_blob_crc(uint8_t *blob, size_t size) {
    assert(blob != NULL && size >= 4U);
    const uint32_t crc = vibe_crc32(blob, size - 4U);
    for (unsigned i = 0; i < 4U; ++i) {
        blob[size - 4U + i] = (uint8_t)(crc >> (8U * i));
    }
}

static vibe_error_t parse_chunks(const char *json, size_t chunk_size,
                                 int32_t day, vibe_day_candidate_t *candidate) {
    vibe_usage_parser_t parser;
    vibe_usage_parser_init(&parser, candidate, day, VIBE_TZ_ASIA_SHANGHAI);
    const size_t length = strlen(json);
    for (size_t offset = 0; offset < length; offset += chunk_size) {
        size_t size = length - offset;
        if (size > chunk_size) size = chunk_size;
        vibe_error_t error = vibe_usage_parser_feed(
            &parser, (const uint8_t *)json + offset, size);
        if (error != VIBE_OK) return error;
    }
    return vibe_usage_parser_finish(&parser);
}

static vibe_error_t parse_bytes(const uint8_t *json, size_t length,
                                size_t chunk_size, size_t max_body_bytes,
                                uint32_t max_buckets, int32_t day,
                                vibe_day_candidate_t *candidate) {
    vibe_usage_parser_t parser;
    vibe_usage_parser_init(&parser, candidate, day, VIBE_TZ_ASIA_SHANGHAI);
    vibe_usage_parser_set_limits(&parser, max_body_bytes, max_buckets);
    for (size_t offset = 0; offset < length; offset += chunk_size) {
        size_t size = length - offset;
        if (size > chunk_size) size = chunk_size;
        vibe_error_t error = vibe_usage_parser_feed(&parser, json + offset,
                                                     size);
        if (error != VIBE_OK) return error;
    }
    return vibe_usage_parser_finish(&parser);
}

static void test_dates(void) {
    char from[25], to[25];
    expect(vibe_date_api_bounds(20260101, VIBE_TZ_ASIA_SHANGHAI, from, to),
           VIBE_OK, "Shanghai inclusive API bounds");
    assert(strcmp(from, "2025-12-31T16:00:00.000Z") == 0);
    assert(strcmp(to, "2026-01-01T15:59:59.999Z") == 0);
    expect(vibe_date_api_bounds(20240229, VIBE_TZ_UTC, from, to),
           VIBE_OK, "UTC leap-day API bounds");
    assert(strcmp(from, "2024-02-29T00:00:00.000Z") == 0);
    assert(strcmp(to, "2024-02-29T23:59:59.999Z") == 0);
    expect(vibe_date_api_bounds(20260229, VIBE_TZ_UTC, from, to),
           VIBE_ERR_INVALID_ARGUMENT, "invalid API day");
    expect(vibe_date_api_bounds(20260905, (vibe_timezone_t)99, from, to),
           VIBE_ERR_INVALID_ARGUMENT, "invalid API timezone");
    for (int zone = 0; zone <= 1; ++zone) {
        int32_t day = 20231225;
        for (unsigned n = 0; n < 800; ++n) {
            expect(vibe_date_api_bounds(day, (vibe_timezone_t)zone, from, to),
                   VIBE_OK, "calendar API bounds");
            int64_t start, end;
            int32_t first_day, last_day, next_day;
            expect(vibe_iso8601_to_epoch(from, &start), VIBE_OK, "API start");
            expect(vibe_iso8601_to_epoch(to, &end), VIBE_OK, "API end");
            assert(end - start == 86399);
            expect(vibe_date_from_epoch(start, (vibe_timezone_t)zone, &first_day),
                   VIBE_OK, "start local day");
            expect(vibe_date_from_epoch(end, (vibe_timezone_t)zone, &last_day),
                   VIBE_OK, "end local day");
            expect(vibe_date_from_epoch(end + 1, (vibe_timezone_t)zone, &next_day),
                   VIBE_OK, "next midnight excluded");
            assert(first_day == day && last_day == day && next_day != day);
            day = next_day;
        }
    }
    int32_t result;
    expect(vibe_date_add_days(20240228, 1, &result), VIBE_OK, "leap +1");
    assert(result == 20240229);
    expect(vibe_date_add_days(20240229, 1, &result), VIBE_OK, "leap +2");
    assert(result == 20240301);
    expect(vibe_date_add_days(20250101, -1, &result), VIBE_OK, "year -1");
    assert(result == 20241231);

    int64_t epoch;
    expect(vibe_iso8601_to_epoch("2026-09-04T16:30:00Z", &epoch), VIBE_OK,
           "ISO Z");
    expect(vibe_date_from_epoch(epoch, VIBE_TZ_ASIA_SHANGHAI, &result), VIBE_OK,
           "Shanghai date");
    assert(result == 20260905);
    expect(vibe_iso8601_to_date("2026-09-05T00:30:00+08:00", VIBE_TZ_UTC,
                                &result),
           VIBE_OK, "offset date");
    assert(result == 20260904);
    expect(vibe_iso8601_to_epoch("2026-02-30T00:00:00Z", &epoch),
           VIBE_ERR_SCHEMA, "bad date");
}

static void test_http_policy(void) {
    assert(vibe_http_content_type_is_json("application/json"));
    assert(vibe_http_content_type_is_json(
        " Application/JSON ; charset=utf-8"));
    assert(!vibe_http_content_type_is_json("text/html"));
    assert(!vibe_http_content_type_is_json("application/problem+json"));

    uint32_t delay = UINT32_MAX;
    assert(vibe_http_parse_retry_after("120", 0, &delay));
    assert(delay == 120);
    assert(vibe_http_parse_retry_after(" 0\t", 0, &delay));
    assert(delay == 0);
    assert(!vibe_http_parse_retry_after("12 seconds", 0, &delay));
    assert(!vibe_http_parse_retry_after("4294967296", 0, &delay));

    int64_t now = 0;
    expect(vibe_iso8601_to_epoch("1994-11-06T08:48:37Z", &now), VIBE_OK,
           "retry reference time");
    assert(vibe_http_parse_retry_after(
        "Sun, 06 Nov 1994 08:49:37 GMT", now, &delay));
    assert(delay == 60);
    assert(!vibe_http_parse_retry_after(
        "Sun, 31 Feb 1994 08:49:37 GMT", now, &delay));
}

static void test_json_cstring_safety(void) {
    static const uint8_t ordinary[] =
        "{\"deviceCode\":\"abc\",\"escaped\":\"quote\\\"ok\"}";
    assert(vibe_json_text_is_cstring_safe(ordinary,
                                          sizeof(ordinary) - 1U));

    static const uint8_t raw_nul[] = {'{', '"', 'x', '"', ':', '"',
                                      'a', 0,   'b', '"', '}'};
    assert(!vibe_json_text_is_cstring_safe(raw_nul, sizeof(raw_nul)));

    static const uint8_t nul_in_value[] = "{\"x\":\"good\\u0000evil\"}";
    assert(!vibe_json_text_is_cstring_safe(nul_in_value,
                                           sizeof(nul_in_value) - 1U));

    static const uint8_t nul_in_key[] = "{\"apiKey\\u0000shadow\":\"x\"}";
    assert(!vibe_json_text_is_cstring_safe(nul_in_key,
                                           sizeof(nul_in_key) - 1U));

    static const uint8_t literal_escape[] = "{\"x\":\"\\\\u0000\"}";
    assert(vibe_json_text_is_cstring_safe(literal_escape,
                                          sizeof(literal_escape) - 1U));

    static const uint8_t slash_then_nul[] = "{\"x\":\"\\\\\\u0000\"}";
    assert(!vibe_json_text_is_cstring_safe(slash_then_nul,
                                           sizeof(slash_then_nul) - 1U));

    assert(vibe_json_text_is_cstring_safe(NULL, 0));
    assert(!vibe_json_text_is_cstring_safe(NULL, 1));
}

static void test_device_flow_policy(void) {
    assert(vibe_device_poll_classify(200, 1, true, false, 0, false, false,
                                     0, false, false) ==
           VIBE_DEVICE_POLL_SHAPE_SUCCESS);
    assert(vibe_device_poll_classify(200, 1, true, false, 1, true, false,
                                     0, false, false) ==
           VIBE_DEVICE_POLL_SHAPE_SUCCESS);
    assert(vibe_device_poll_classify(200, 1, true, false, 1, false, true,
                                     0, false, false) ==
           VIBE_DEVICE_POLL_SHAPE_SUCCESS);
    assert(vibe_device_poll_classify(200, 1, true, false, 1, false, false,
                                     0, false, false) ==
           VIBE_DEVICE_POLL_SHAPE_INVALID);
    assert(vibe_device_poll_classify(200, 1, false, false, 0, false, false,
                                     0, false, false) ==
           VIBE_DEVICE_POLL_SHAPE_INVALID);
    assert(vibe_device_poll_classify(200, 2, true, true, 0, false, false,
                                     0, false, false) ==
           VIBE_DEVICE_POLL_SHAPE_INVALID);
    assert(vibe_device_poll_classify(200, 1, true, false, 0, false, false,
                                     1, true, false) ==
           VIBE_DEVICE_POLL_SHAPE_INVALID);
    assert(vibe_device_poll_classify(200, 0, false, false, 0, false, false,
                                     1, true, false) ==
           VIBE_DEVICE_POLL_SHAPE_ERROR);
    assert(vibe_device_poll_classify(200, 1, false, true, 1, false, true,
                                     1, true, false) ==
           VIBE_DEVICE_POLL_SHAPE_ERROR);
    assert(vibe_device_poll_classify(200, 0, false, false, 1, true, false,
                                     1, true, false) ==
           VIBE_DEVICE_POLL_SHAPE_INVALID);
    assert(vibe_device_poll_classify(410, 0, false, false, 0, false, false,
                                     0, false, false) ==
           VIBE_DEVICE_POLL_SHAPE_ALREADY_DELIVERED);
    assert(vibe_device_poll_classify(410, 1, false, true, 1, false, true,
                                     1, false, true) ==
           VIBE_DEVICE_POLL_SHAPE_ALREADY_DELIVERED);
    assert(vibe_device_poll_classify(410, 0, false, false, 0, false, false,
                                     1, true, false) ==
           VIBE_DEVICE_POLL_SHAPE_ALREADY_DELIVERED);
    assert(vibe_device_poll_classify(410, 0, false, false, 0, false, false,
                                     1, false, false) ==
           VIBE_DEVICE_POLL_SHAPE_INVALID);
    assert(vibe_device_poll_classify(500, 0, false, false, 0, false, false,
                                     1, true, false) ==
           VIBE_DEVICE_POLL_SHAPE_INVALID);

    const char *origin = "https://vibecafe.ai";
    assert(vibe_device_flow_url_is_trusted(
        "https://vibecafe.ai/device?code=ABC-123", origin));
    assert(!vibe_device_flow_url_is_trusted("https://vibecafe.ai", origin));
    assert(!vibe_device_flow_url_is_trusted(
        "https://vibecafe.ai.evil.example/device", origin));
    assert(!vibe_device_flow_url_is_trusted(
        "https://vibecafe.ai/device#fragment", origin));
    assert(!vibe_device_flow_url_is_trusted(
        "https://vibecafe.ai/device\\redirect", origin));
    assert(!vibe_device_flow_url_is_trusted(
        "https://vibecafe.ai/device\nnext", origin));
    assert(!vibe_device_flow_url_is_trusted(
        "https://vibecafe.ai/device@evil.example", origin));
    assert(!vibe_device_flow_url_is_trusted(NULL, origin));

    assert(vibe_device_flow_api_key_is_valid("vbu_abc-DEF_123", 512));
    assert(!vibe_device_flow_api_key_is_valid("vbu_", 512));
    assert(!vibe_device_flow_api_key_is_valid("other_abc", 512));
    assert(!vibe_device_flow_api_key_is_valid("vbu_abc\r\nInjected", 512));
    assert(!vibe_device_flow_api_key_is_valid("vbu_abc.def", 512));
    assert(!vibe_device_flow_api_key_is_valid("vbu_abc", 7));
    assert(!vibe_device_flow_api_key_is_valid(NULL, 512));
}

static void test_parser_and_chunk_boundaries(void) {
    const char *json =
        "{\"buckets\":["
        "{\"source\":\"codex\",\"model\":\"x\",\"project\":{\"deep\":[1,true,null]},"
        "\"bucketStart\":\"2026-09-04T16:30:00Z\",\"totalTokens\":9007199254740993},"
        "{\"totalTokens\":7,\"bucketStart\":\"2026-09-05T08:00:00+08:00\","
        "\"source\":\"claude\\u002dcode\"}],\"sessions\":[],\"hasAnyData\":true}";
    vibe_day_candidate_t expected;
    expect(parse_chunks(json, strlen(json), 20260905, &expected), VIBE_OK,
           "whole JSON");
    assert(expected.total_tokens == UINT64_C(9007199254741000));
    assert(expected.source_count == 2);
    assert(strcmp(expected.sources[0].id, "codex") == 0);
    assert(strcmp(expected.sources[1].id, "claude-code") == 0);
    assert(expected.has_any_data);

    for (size_t chunk = 1; chunk <= strlen(json); ++chunk) {
        vibe_day_candidate_t actual;
        expect(parse_chunks(json, chunk, 20260905, &actual), VIBE_OK,
               "chunked JSON");
        assert(actual.total_tokens == expected.total_tokens);
        assert(actual.source_count == expected.source_count);
    }
}

static void test_parser_rejections(void) {
    static const struct {
        const char *json;
        vibe_error_t error;
    } cases[] = {
        {"{\"buckets\":[{\"source\":\"x\",\"bucketStart\":\"2026-09-05T00:00:00+08:00\",\"totalTokens\":-1}],\"hasAnyData\":true}", VIBE_ERR_SCHEMA},
        {"{\"buckets\":[{\"source\":\"x\",\"bucketStart\":\"2026-09-05T00:00:00+08:00\",\"totalTokens\":1.5}],\"hasAnyData\":true}", VIBE_ERR_SCHEMA},
        {"{\"buckets\":[{\"source\":\"x\",\"bucketStart\":\"2026-09-05T00:00:00+08:00\"}],\"hasAnyData\":true}", VIBE_ERR_SCHEMA},
        {"{\"buckets\":[{\"source\":\"x\",\"source\":\"y\",\"bucketStart\":\"2026-09-05T00:00:00+08:00\",\"totalTokens\":1}],\"hasAnyData\":true}", VIBE_ERR_SCHEMA},
        {"{\"buckets\":[{\"source\":\"x\",\"bucketStart\":\"2026-09-04T00:00:00+08:00\",\"totalTokens\":1}],\"hasAnyData\":true}", VIBE_ERR_OUT_OF_RANGE},
        {"{\"buckets\":[],\"hasAnyData\":false,}", VIBE_ERR_INVALID_JSON},
        {"{\"buckets\":[,],\"hasAnyData\":false}", VIBE_ERR_INVALID_JSON},
        {"{\"buckets\":[],\"hasAnyData\":false}garbage", VIBE_ERR_INVALID_JSON},
        {"{\"buckets\":[]}", VIBE_ERR_SCHEMA},
        {"{\"buckets\\u0000fake\":[],\"hasAnyData\":false}", VIBE_ERR_SCHEMA},
        {"{\"buckets\":[],\"hasAnyData\\u0000fake\":false}", VIBE_ERR_SCHEMA},
        {"{\"buckets\":[],\"buckets\":[],\"hasAnyData\":false}", VIBE_ERR_SCHEMA},
        {"{\"buckets\":[],\"hasAnyData\":false,\"hasAnyData\":false}", VIBE_ERR_SCHEMA},
        {"{\"buckets\":[{\"source\":\"co\\u0000dex\",\"bucketStart\":\"2026-09-05T00:00:00+08:00\",\"totalTokens\":1}],\"hasAnyData\":true}", VIBE_ERR_SCHEMA},
        {"{\"buckets\":[{\"source\":\"codex\",\"bucketStart\":\"2026-09-05T00:00:00+08:00\\u0000junk\",\"totalTokens\":1}],\"hasAnyData\":true}", VIBE_ERR_SCHEMA},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        vibe_day_candidate_t candidate;
        char context[48];
        snprintf(context, sizeof(context), "invalid fixture %zu", i);
        expect(parse_chunks(cases[i].json, 1, 20260905, &candidate),
               cases[i].error, context);
    }

    const char *partial =
        "{\"buckets\":[{\"source\":\"x\",\"bucketStart\":"
        "\"2026-09-05T00:00:00+08:00\",\"totalTokens\":1";
    vibe_day_candidate_t candidate;
    expect(parse_chunks(partial, 3, 20260905, &candidate),
           VIBE_ERR_INVALID_JSON, "partial body");
}

static void test_parser_utf8_and_completeness_guards(void) {
    static const char valid_utf8[] =
        "{\"buckets\":[{\"source\":\"codex-\xe4\xb8\xad\","
        "\"bucketStart\":\"2026-09-05T00:00:00+08:00\","
        "\"totalTokens\":1}],\"hasAnyData\":true}";
    vibe_day_candidate_t candidate;
    expect(parse_bytes((const uint8_t *)valid_utf8, sizeof(valid_utf8) - 1U,
                       1, 4096, 10, 20260905, &candidate),
           VIBE_OK, "valid raw UTF-8 across chunks");
    assert(candidate.source_count == 1);
    assert(strcmp(candidate.sources[0].id, "codex-\xe4\xb8\xad") == 0);

    static const char invalid_overlong[] =
        "{\"buckets\":[],\"hasAnyData\":false,\"x\":\"\xc0\xaf\"}";
    expect(parse_bytes((const uint8_t *)invalid_overlong,
                       sizeof(invalid_overlong) - 1U, 1, 4096, 10, 20260905,
                       &candidate),
           VIBE_ERR_INVALID_JSON, "reject overlong UTF-8");

    static const char invalid_continuation[] =
        "{\"buckets\":[],\"hasAnyData\":false,\"x\":\"\xe2(\xa1\"}";
    expect(parse_bytes((const uint8_t *)invalid_continuation,
                       sizeof(invalid_continuation) - 1U, 1, 4096, 10,
                       20260905, &candidate),
           VIBE_ERR_INVALID_JSON, "reject invalid UTF-8 continuation");

    static const char invalid_surrogate[] =
        "{\"buckets\":[],\"hasAnyData\":false,\"x\":\"\xed\xa0\x80\"}";
    expect(parse_bytes((const uint8_t *)invalid_surrogate,
                       sizeof(invalid_surrogate) - 1U, 1, 4096, 10,
                       20260905, &candidate),
           VIBE_ERR_INVALID_JSON, "reject raw UTF-8 surrogate");

    static const char *const pagination_cases[] = {
        "{\"buckets\":[],\"hasAnyData\":false,\"next\":null}",
        "{\"buckets\":[],\"hasAnyData\":false,\"nextCursor\":\"x\"}",
        "{\"buckets\":[],\"hasAnyData\":false,\"cursor\":0}",
        "{\"buckets\":[],\"hasAnyData\":false,\"truncated\":false}",
        "{\"buckets\":[],\"hasAnyData\":false,\"hasMore\":false}",
        "{\"buckets\":[],\"hasAnyData\":false,\"pagination\":{}}",
    };
    for (size_t i = 0;
         i < sizeof(pagination_cases) / sizeof(pagination_cases[0]); ++i) {
        expect(parse_chunks(pagination_cases[i], 1, 20260905, &candidate),
               VIBE_ERR_SCHEMA, "reject unimplemented pagination signal");
    }
}

static void test_parser_resource_and_integer_limits(void) {
    static const char one_bucket[] =
        "{\"buckets\":[{\"source\":\"x\","
        "\"bucketStart\":\"2026-09-05T00:00:00+08:00\","
        "\"totalTokens\":1}],\"hasAnyData\":true}";
    vibe_day_candidate_t candidate;
    expect(parse_bytes((const uint8_t *)one_bucket, sizeof(one_bucket) - 1U,
                       7, sizeof(one_bucket) - 2U, 10, 20260905, &candidate),
           VIBE_ERR_BODY_TOO_LARGE, "body byte limit");

    static const char two_buckets[] =
        "{\"buckets\":["
        "{\"source\":\"x\",\"bucketStart\":\"2026-09-05T00:00:00+08:00\",\"totalTokens\":1},"
        "{\"source\":\"y\",\"bucketStart\":\"2026-09-05T01:00:00+08:00\",\"totalTokens\":1}],"
        "\"hasAnyData\":true}";
    expect(parse_bytes((const uint8_t *)two_buckets,
                       sizeof(two_buckets) - 1U, 3, 4096, 1, 20260905,
                       &candidate),
           VIBE_ERR_TOO_MANY_BUCKETS, "bucket count limit");

    char deep[256];
    size_t used = (size_t)snprintf(
        deep, sizeof(deep),
        "{\"buckets\":[],\"hasAnyData\":false,\"deep\":");
    for (unsigned i = 0; i < VIBE_PARSER_MAX_DEPTH; ++i) deep[used++] = '[';
    deep[used++] = '0';
    for (unsigned i = 0; i < VIBE_PARSER_MAX_DEPTH; ++i) deep[used++] = ']';
    deep[used++] = '}';
    deep[used] = '\0';
    expect(parse_chunks(deep, 2, 20260905, &candidate),
           VIBE_ERR_INVALID_JSON, "nesting depth limit");

    static const char max_value[] =
        "{\"buckets\":[{\"source\":\"x\","
        "\"bucketStart\":\"2026-09-05T00:00:00+08:00\","
        "\"totalTokens\":18446744073709551615}],\"hasAnyData\":true}";
    expect(parse_chunks(max_value, 1, 20260905, &candidate), VIBE_OK,
           "UINT64_MAX exact parse");
    assert(candidate.total_tokens == UINT64_MAX);

    static const char lexical_overflow[] =
        "{\"buckets\":[{\"source\":\"x\","
        "\"bucketStart\":\"2026-09-05T00:00:00+08:00\","
        "\"totalTokens\":18446744073709551616}],\"hasAnyData\":true}";
    expect(parse_chunks(lexical_overflow, 1, 20260905, &candidate),
           VIBE_ERR_OVERFLOW, "uint64 lexical overflow");

    static const char sum_overflow[] =
        "{\"buckets\":["
        "{\"source\":\"x\",\"bucketStart\":\"2026-09-05T00:00:00+08:00\",\"totalTokens\":18446744073709551615},"
        "{\"source\":\"x\",\"bucketStart\":\"2026-09-05T01:00:00+08:00\",\"totalTokens\":1}],"
        "\"hasAnyData\":true}";
    expect(parse_chunks(sum_overflow, 1, 20260905, &candidate),
           VIBE_ERR_OVERFLOW, "bucket sum overflow");
}

static void test_aggregate_overflow_and_other(void) {
    vibe_day_candidate_t candidate;
    vibe_day_candidate_init(&candidate, 20260905);
    for (unsigned i = 0; i < 30; ++i) {
        char source[24];
        snprintf(source, sizeof(source), "source-%02u", i);
        expect(vibe_day_candidate_add(&candidate, source, i + 1U), VIBE_OK,
               "source add");
    }
    assert(candidate.source_count == VIBE_SOURCE_SLOTS);
    assert(candidate.sources_collapsed);
    assert(strcmp(candidate.sources[VIBE_SOURCE_SLOTS - 1U].id,
                  VIBE_OTHER_SOURCE_ID) == 0);
    assert(candidate.total_tokens == 465);

    vibe_day_candidate_init(&candidate, 20260905);
    expect(vibe_day_candidate_add(&candidate, "x", UINT64_MAX), VIBE_OK,
           "max add");
    expect(vibe_day_candidate_add(&candidate, "x", 1), VIBE_ERR_OVERFLOW,
           "overflow add");
    assert(vibe_basis_points(UINT64_MAX - 1U, UINT64_MAX) == 9999);
    assert(vibe_basis_points(1, 3) == 3333);
}

static void test_cache_replacement_snapshot_and_codec(void) {
    vibe_cache_t cache;
    vibe_cache_init(&cache, 7, 2, VIBE_TZ_ASIA_SHANGHAI);

    vibe_day_candidate_t invalid_freshness;
    vibe_day_candidate_init(&invalid_freshness, 20260905);
    expect(vibe_cache_replace_day(&cache, &invalid_freshness, 0),
           VIBE_ERR_INVALID_ARGUMENT, "zero fetch timestamp");

    for (unsigned age = 0; age < 7; ++age) {
        int32_t day;
        expect(vibe_date_add_days(20260905, -(int)age, &day), VIBE_OK,
               "cache date");
        vibe_day_candidate_t candidate;
        vibe_day_candidate_init(&candidate, day);
        candidate.has_any_data = age != 0;
        expect(vibe_day_candidate_add(&candidate, "codex", 100 + age), VIBE_OK,
               "cache codex");
        expect(vibe_day_candidate_add(&candidate, "claude-code", 50 + age),
               VIBE_OK, "cache claude");
        expect(vibe_cache_replace_day(&cache, &candidate, 1000 + age), VIBE_OK,
               "cache replace");
    }

    vibe_usage_snapshot_t snapshot;
    expect(vibe_cache_build_snapshot(&cache, 20260905, VIBE_WINDOW_TODAY,
                                     &snapshot),
           VIBE_OK, "snapshot");
    assert(snapshot.today_tokens == 150);
    assert(snapshot.seven_day_tokens == 1092);
    assert(snapshot.today_complete && snapshot.seven_day_complete);
    assert(snapshot.valid_days_mask == 0x7f);
    assert(snapshot.state == VIBE_READY);
    assert(snapshot.agent_count == 2);

    /* Identical data advances volatile freshness without burning another A/B
     * sequence; the client persists a freshness checkpoint at most hourly. */
    const uint32_t unchanged_sequence = cache.sequence;
    vibe_day_candidate_t unchanged;
    vibe_day_candidate_init(&unchanged, 20260905);
    unchanged.has_any_data = false;
    expect(vibe_day_candidate_add(&unchanged, "codex", 100), VIBE_OK,
           "unchanged codex");
    expect(vibe_day_candidate_add(&unchanged, "claude-code", 50), VIBE_OK,
           "unchanged claude");
    expect(vibe_cache_replace_day(&cache, &unchanged, 1500), VIBE_OK,
           "unchanged replace");
    assert(cache.sequence == unchanged_sequence);
    assert(vibe_cache_find_day(&cache, 20260905)->fetched_at == 1500);

    /* A complete lower revision replaces the day; it is never accumulated. */
    vibe_day_candidate_t revised;
    vibe_day_candidate_init(&revised, 20260905);
    revised.has_any_data = false;
    expect(vibe_day_candidate_add(&revised, "codex", 0), VIBE_OK,
           "zero revision");
    expect(vibe_cache_replace_day(&cache, &revised, 2000), VIBE_OK,
           "replace revision");
    expect(vibe_cache_build_snapshot(&cache, 20260905, VIBE_WINDOW_TODAY,
                                     &snapshot),
           VIBE_OK, "zero snapshot");
    assert(snapshot.today_tokens == 0);
    assert(snapshot.state == VIBE_EMPTY);
    assert(snapshot.last_fetch_at == 2000);
    expect(vibe_cache_build_snapshot(&cache, 20260905, VIBE_WINDOW_SEVEN_DAYS,
                                     &snapshot), VIBE_OK, "7D nonempty with empty today");
    assert(snapshot.today_tokens == 0 && snapshot.seven_day_tokens > 0);
    assert(snapshot.seven_day_complete && snapshot.state == VIBE_READY);

    uint8_t blob[VIBE_CACHE_BLOB_MAX_BYTES];
    size_t written = 0;
    expect(vibe_cache_encode(&cache, blob, sizeof(blob), &written), VIBE_OK,
           "encode");
    assert(written < VIBE_CACHE_BLOB_MAX_BYTES);
    vibe_cache_t decoded;
    expect(vibe_cache_decode(blob, written, &decoded), VIBE_OK, "decode");
    assert(decoded.persisted);
    assert(decoded.sequence == cache.sequence);
    assert(vibe_cache_find_day(&decoded, 20260905)->total_tokens == 0);
    blob[4] = 1U;
    refresh_blob_crc(blob, written);
    expect(vibe_cache_decode(blob, written, &decoded), VIBE_ERR_VERSION,
           "legacy date-only cache must be reconciled again");
    expect(vibe_cache_encode(&cache, blob, sizeof(blob), &written), VIBE_OK,
           "restore current cache version");
    blob[written / 2] ^= 0x80;
    expect(vibe_cache_decode(blob, written, &decoded), VIBE_ERR_CRC,
           "CRC corruption");

    expect(vibe_cache_encode(&cache, blob, sizeof(blob), &written), VIBE_OK,
           "re-encode after corruption");
    blob[41] = 0x80U;
    refresh_blob_crc(blob, written);
    expect(vibe_cache_decode(blob, written, &decoded), VIBE_ERR_SCHEMA,
           "pending mask outside seven days");

    expect(vibe_cache_encode(&cache, blob, sizeof(blob), &written), VIBE_OK,
           "re-encode before timestamp mutation");
    memset(blob + 29U, 0xff, 8U);
    refresh_blob_crc(blob, written);
    expect(vibe_cache_decode(blob, written, &decoded), VIBE_ERR_SCHEMA,
           "unsigned timestamp outside int64 range");
}

static void test_snapshot_ranking_and_other(void) {
    vibe_cache_t cache;
    vibe_cache_init(&cache, 1, 1, VIBE_TZ_UTC);

    vibe_day_candidate_t candidate;
    vibe_day_candidate_init(&candidate, 20260905);
    candidate.has_any_data = true;
    for (int letter = 11; letter >= 0; --letter) {
        char source[] = "source-a";
        source[7] = (char)('a' + letter);
        expect(vibe_day_candidate_add(&candidate, source, 100), VIBE_OK,
               "ranking source add");
    }
    expect(vibe_cache_replace_day(&cache, &candidate, 1000), VIBE_OK,
           "ranking cache replace");

    vibe_usage_snapshot_t snapshot;
    expect(vibe_cache_build_snapshot(&cache, 20260905, VIBE_WINDOW_TODAY,
                                     &snapshot),
           VIBE_OK, "ranking snapshot");
    assert(snapshot.agent_count == VIBE_MAX_AGENTS);
    assert(snapshot.sources_collapsed);
    assert(snapshot.today_tokens == 1200);
    assert(snapshot.seven_day_tokens == 1200);

    uint64_t today_sum = 0;
    uint64_t seven_sum = 0;
    for (size_t i = 0; i < VIBE_MAX_AGENTS - 1U; ++i) {
        char expected[] = "source-a";
        expected[7] = (char)('a' + (int)i);
        assert(strcmp(snapshot.agents[i].id, expected) == 0);
        today_sum += snapshot.agents[i].today_tokens;
        seven_sum += snapshot.agents[i].seven_day_tokens;
    }
    const vibe_agent_usage_t *other =
        &snapshot.agents[VIBE_MAX_AGENTS - 1U];
    assert(strcmp(other->id, VIBE_OTHER_SOURCE_ID) == 0);
    assert(other->today_tokens == 100);
    assert(other->seven_day_tokens == 100);
    today_sum += other->today_tokens;
    seven_sum += other->seven_day_tokens;
    assert(today_sum == snapshot.today_tokens);
    assert(seven_sum == snapshot.seven_day_tokens);
}

static void test_cache_maximum_codec_shape(void) {
    vibe_cache_t cache;
    vibe_cache_init(&cache, UINT32_MAX, UINT32_MAX, VIBE_TZ_UTC);
    for (size_t source = 0; source < VIBE_SOURCE_SLOTS - 1U; ++source) {
        memset(cache.source_ids[source], 'a' + (int)(source % 26U),
               VIBE_SOURCE_ID_BYTES - 1U);
        cache.source_ids[source][0] = (char)('A' + (int)(source % 26U));
        cache.source_ids[source][VIBE_SOURCE_ID_BYTES - 1U] = '\0';
    }
    for (size_t day = 0; day < VIBE_CACHE_DAYS; ++day) {
        int32_t date_key = 0;
        expect(vibe_date_add_days(20260905, -(int)day, &date_key), VIBE_OK,
               "max-shape cache date");
        cache.days[day].valid = true;
        cache.days[day].has_any_data = true;
        cache.days[day].date_key = date_key;
        cache.days[day].fetched_at = INT64_MAX - (int64_t)day;
        cache.days[day].total_tokens = UINT64_MAX;
        cache.days[day].source_tokens[day % VIBE_SOURCE_SLOTS] = UINT64_MAX;
    }
    cache.last_reconcile_at = INT64_MAX;
    cache.reconcile_anchor_key = 20260905;
    cache.reconcile_pending_mask = 0x7fU;

    uint8_t blob[VIBE_CACHE_BLOB_MAX_BYTES];
    size_t written = 0;
    expect(vibe_cache_encode(&cache, blob, sizeof(blob), &written), VIBE_OK,
           "max-shape cache encode");
    assert(written > 2048U && written <= sizeof(blob));
    vibe_cache_t decoded;
    expect(vibe_cache_decode(blob, written, &decoded), VIBE_OK,
           "max-shape cache decode");
    assert(decoded.days[VIBE_CACHE_DAYS - 1U].total_tokens == UINT64_MAX);
    expect(vibe_cache_encode(&cache, blob, written - 1U, &written),
           VIBE_ERR_BUFFER_TOO_SMALL, "max-shape exact capacity guard");
}

static void test_missing_days_are_incomplete(void) {
    vibe_cache_t cache;
    vibe_cache_init(&cache, 1, 1, VIBE_TZ_UTC);
    for (unsigned age = 0; age < 3; ++age) {
        int32_t day;
        assert(vibe_date_add_days(20260905, -(int)age, &day) == VIBE_OK);
        vibe_day_candidate_t candidate;
        vibe_day_candidate_init(&candidate, day);
        candidate.has_any_data = true;
        assert(vibe_day_candidate_add(&candidate, "codex", 10) == VIBE_OK);
        assert(vibe_cache_replace_day(&cache, &candidate, 1) == VIBE_OK);
    }
    vibe_usage_snapshot_t snapshot;
    assert(vibe_cache_build_snapshot(&cache, 20260905,
                                     VIBE_WINDOW_SEVEN_DAYS,
                                     &snapshot) == VIBE_OK);
    assert(snapshot.valid_days_mask == 0x07);
    assert(!snapshot.seven_day_complete);
    assert(snapshot.seven_day_tokens == 30);
    assert(snapshot.state == VIBE_STALE);

    for (unsigned age = 0; age < 7; ++age) {
        int32_t day;
        assert(vibe_date_add_days(20260905, -(int)age, &day) == VIBE_OK);
        vibe_day_candidate_t empty;
        vibe_day_candidate_init(&empty, day);
        assert(vibe_cache_replace_day(&cache, &empty, 2) == VIBE_OK);
    }
    assert(vibe_cache_build_snapshot(&cache, 20260905, VIBE_WINDOW_SEVEN_DAYS,
                                     &snapshot) == VIBE_OK);
    assert(snapshot.seven_day_complete && snapshot.state == VIBE_EMPTY);
}

static void test_fixture_file(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    const long length = ftell(file);
    assert(length > 0);
    rewind(file);
    char *json = malloc((size_t)length + 1U);
    assert(json != NULL);
    assert(fread(json, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    json[length] = '\0';
    vibe_day_candidate_t candidate;
    expect(parse_chunks(json, 7, 20260905, &candidate), VIBE_OK,
           "fixture file");
    assert(candidate.total_tokens == UINT64_C(9007199254741000));
    free(json);
}

static void test_state_reducer(void) {
    vibe_app_model_t model;
    vibe_app_model_init(&model);
    vibe_app_event_t event = {.type = VIBE_EVENT_BOOT_LOADED, .flag = false};
    assert(vibe_app_reduce(&model, &event) == VIBE_EFFECT_NONE);
    assert(model.lifecycle == VIBE_APP_WIFI_REQUIRED);

    event.type = VIBE_EVENT_USER_START_WIFI;
    uint32_t effects = vibe_app_reduce(&model, &event);
    assert((effects & VIBE_EFFECT_START_PORTAL) != 0);
    const uint32_t operation = model.operation_revision;

    event.type = VIBE_EVENT_WIFI_GOT_IP;
    vibe_app_reduce(&model, &event);
    event.type = VIBE_EVENT_TIME_VALID;
    effects = vibe_app_reduce(&model, &event);
    assert((effects & VIBE_EFFECT_REQUEST_CODE) != 0);

    event.type = VIBE_EVENT_AUTH_SUCCESS;
    event.generation = model.generation;
    event.config_revision = model.config_revision;
    effects = vibe_app_reduce(&model, &event);
    assert((effects & VIBE_EFFECT_PERSIST_AUTH) != 0);
    assert(model.has_auth);

    event.type = VIBE_EVENT_TIMEZONE_CHANGED;
    effects = vibe_app_reduce(&model, &event);
    assert(model.operation_revision > operation);
    assert((effects & VIBE_EFFECT_INVALIDATE_CACHE) != 0);

    const uint32_t stale_generation = model.generation;
    event.type = VIBE_EVENT_USER_RELINK;
    vibe_app_reduce(&model, &event);
    assert(model.generation == stale_generation + 1U);
    event.type = VIBE_EVENT_HTTP_SUCCESS;
    event.generation = stale_generation;
    event.config_revision = model.config_revision;
    assert(vibe_app_reduce(&model, &event) == VIBE_EFFECT_DISCARD_RESULT);
    assert(!model.has_lkg);

    event.type = VIBE_EVENT_HTTP_401;
    effects = vibe_app_reduce(&model, &event);
    assert((effects & VIBE_EFFECT_PERSIST_TOMBSTONE) != 0);
    assert(model.data_state == VIBE_AUTH_REQUIRED);
}

static void test_epd_refresh_policy(void) {
    enum { WIDTH = 16, HEIGHT = 4, STRIDE = 2, FRAME_SIZE = 8 };
    uint8_t previous[FRAME_SIZE];
    uint8_t current[FRAME_SIZE];
    memset(previous, 0xff, sizeof(previous));
    memset(current, 0xff, sizeof(current));
    note4_epd_refresh_plan_t plan;

    assert(note4_epd_plan_refresh(current, previous, sizeof(current), WIDTH,
                                  HEIGHT, STRIDE, true, false, false, false,
                                  0, &plan));
    assert(plan.action == NOTE4_EPD_REFRESH_SKIP);

    assert(note4_epd_plan_refresh(current, previous, sizeof(current), WIDTH,
                                  HEIGHT, STRIDE, false, false, false, false,
                                  0, &plan));
    assert(plan.action == NOTE4_EPD_REFRESH_FULL);
    assert(plan.width == WIDTH && plan.height == HEIGHT);

    assert(note4_epd_plan_refresh(current, previous, sizeof(current), WIDTH,
                                  HEIGHT, STRIDE, true, false, true, false,
                                  0, &plan));
    assert(plan.action == NOTE4_EPD_REFRESH_FULL);

    current[STRIDE + 1] = 0x7f;
    assert(note4_epd_plan_refresh(current, previous, sizeof(current), WIDTH,
                                  HEIGHT, STRIDE, true, false, false, false,
                                  0, &plan));
    assert(plan.action == NOTE4_EPD_REFRESH_PARTIAL);
    assert(plan.x == 8 && plan.y == 1 && plan.width == 8 && plan.height == 1);
    assert(plan.row_bytes == 1 && plan.changed_area == 8);

    assert(note4_epd_plan_refresh(current, previous, sizeof(current), WIDTH,
                                  HEIGHT, STRIDE, true, false, false, false,
                                  10, &plan));
    assert(plan.action == NOTE4_EPD_REFRESH_FULL);

    memset(current, 0xff, sizeof(current));
    assert(note4_epd_plan_refresh(current, previous, sizeof(current), WIDTH,
                                  HEIGHT, STRIDE, true, false, false, false,
                                  10, &plan));
    assert(plan.action == NOTE4_EPD_REFRESH_SKIP);

    current[0] = 0x7f;
    current[(HEIGHT - 1) * STRIDE + 1] = 0x7f;
    assert(note4_epd_plan_refresh(current, previous, sizeof(current), WIDTH,
                                  HEIGHT, STRIDE, true, false, false, false,
                                  0, &plan));
    assert(plan.action == NOTE4_EPD_REFRESH_FULL);
    assert(plan.changed_area == WIDTH * HEIGHT);

    assert(!note4_epd_plan_refresh(current, previous, sizeof(current) - 1,
                                   WIDTH, HEIGHT, STRIDE, true, false, false,
                                   false, 0, &plan));
}

int main(int argc, char **argv) {
    const uint8_t list_sizes[] = {3, 4, 6, 8}; /* TODO and Agents, both boards. */
    for (unsigned size = 0; size < sizeof(list_sizes) / sizeof(list_sizes[0]); ++size) {
        const uint8_t visible = list_sizes[size];
        for (uint8_t count = 0; count <= VIBE_MAX_AGENTS; ++count) {
            uint8_t offset = 0;
            const uint8_t limit = count > visible ? count - visible : 0;
            for (uint8_t i = 0; i < limit; ++i) {
                assert(vibe_list_step(&offset, count, visible, true));
                assert(offset == i + 1);
            }
            assert(!vibe_list_step(&offset, count, visible, true));
            for (uint8_t i = limit; i > 0; --i) {
                assert(vibe_list_step(&offset, count, visible, false));
                assert(offset == i - 1);
            }
            assert(!vibe_list_step(&offset, count, visible, false));
            assert(vibe_list_clamp(UINT8_MAX, count, visible) == limit);
        }
    }
    test_dates();
    test_http_policy();
    test_json_cstring_safety();
    test_device_flow_policy();
    test_parser_and_chunk_boundaries();
    test_parser_rejections();
    test_parser_utf8_and_completeness_guards();
    test_parser_resource_and_integer_limits();
    test_aggregate_overflow_and_other();
    test_cache_replacement_snapshot_and_codec();
    test_snapshot_ranking_and_other();
    test_cache_maximum_codec_shape();
    test_missing_days_are_incomplete();
    test_state_reducer();
    test_epd_refresh_policy();
    if (argc > 1) test_fixture_file(argv[1]);
    puts("vibe_usage core host tests: PASS");
    return 0;
}
