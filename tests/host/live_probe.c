/* In-memory bridge for the opt-in real API probe. Never prints personal data. */
#include <stdlib.h>
#include <string.h>
#include "vibe_cache.h"
#include "vibe_parser.h"

void *vibe_probe_create(void) {
    vibe_cache_t *cache = calloc(1, sizeof(*cache));
    if (cache != NULL) vibe_cache_init(cache, 1, 1, VIBE_TZ_ASIA_SHANGHAI);
    return cache;
}

int vibe_probe_day(void *context, const uint8_t *body, size_t size,
                   int32_t day, uint64_t *total) {
    vibe_usage_parser_t *parser = calloc(1, sizeof(*parser));
    vibe_day_candidate_t *candidate = calloc(1, sizeof(*candidate));
    if (parser == NULL || candidate == NULL) {
        free(parser);
        free(candidate);
        return VIBE_ERR_NO_MEMORY;
    }
    vibe_usage_parser_init(parser, candidate, day, VIBE_TZ_ASIA_SHANGHAI);
    vibe_error_t error = VIBE_OK;
    for (size_t offset = 0; offset < size && error == VIBE_OK;) {
        size_t chunk = size - offset;
        if (chunk > 113) chunk = 113;
        error = vibe_usage_parser_feed(parser, body + offset, chunk);
        offset += chunk;
    }
    if (error == VIBE_OK) error = vibe_usage_parser_finish(parser);
    if (error == VIBE_OK) {
        *total = candidate->total_tokens;
        error = vibe_cache_replace_day(context, candidate, 1788566400);
    }
    memset(parser, 0, sizeof(*parser));
    memset(candidate, 0, sizeof(*candidate));
    free(parser);
    free(candidate);
    return error;
}

int vibe_probe_snapshot(void *context, int32_t today, uint64_t *today_total,
                        uint64_t *week_total, uint8_t *mask) {
    vibe_usage_snapshot_t snapshot;
    vibe_error_t error = vibe_cache_build_snapshot(
        context, today, VIBE_WINDOW_SEVEN_DAYS, &snapshot);
    if (error == VIBE_OK) {
        *today_total = snapshot.today_tokens;
        *week_total = snapshot.seven_day_tokens;
        *mask = snapshot.valid_days_mask;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    return error;
}

void vibe_probe_destroy(void *context) {
    if (context != NULL) memset(context, 0, sizeof(vibe_cache_t));
    free(context);
}
