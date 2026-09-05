#ifndef VIBE_CACHE_H_
#define VIBE_CACHE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vibe_aggregate.h"
#include "vibe_usage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t date_key;
    int64_t fetched_at;
    uint64_t total_tokens;
    uint64_t source_tokens[VIBE_SOURCE_SLOTS];
    bool valid;
    bool has_any_data;
    bool sources_collapsed;
} vibe_daily_slot_t;

typedef struct {
    uint32_t schema_version;
    uint32_t sequence;
    uint32_t generation;
    uint32_t config_revision;
    uint32_t metric_id;
    vibe_timezone_t timezone;
    char source_ids[VIBE_SOURCE_SLOTS][VIBE_SOURCE_ID_BYTES];
    vibe_daily_slot_t days[VIBE_CACHE_DAYS];
    int64_t last_reconcile_at;
    int32_t reconcile_anchor_key;
    uint8_t reconcile_pending_mask;
    bool persisted;
} vibe_cache_t;

void vibe_cache_init(vibe_cache_t *cache, uint32_t generation,
                     uint32_t config_revision, vibe_timezone_t timezone);
vibe_error_t vibe_cache_replace_day(vibe_cache_t *cache,
                                    const vibe_day_candidate_t *candidate,
                                    int64_t fetched_at);
const vibe_daily_slot_t *vibe_cache_find_day(const vibe_cache_t *cache,
                                             int32_t date_key);
vibe_error_t vibe_cache_build_snapshot(const vibe_cache_t *cache,
                                       int32_t today_key,
                                       vibe_window_t selected_window,
                                       vibe_usage_snapshot_t *snapshot);
vibe_error_t vibe_cache_encode(const vibe_cache_t *cache, uint8_t *output,
                               size_t output_size, size_t *written);
vibe_error_t vibe_cache_decode(const uint8_t *input, size_t input_size,
                               vibe_cache_t *cache);
uint32_t vibe_crc32(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif  // VIBE_CACHE_H_
