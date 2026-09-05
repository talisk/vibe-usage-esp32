#ifndef VIBE_AGGREGATE_H_
#define VIBE_AGGREGATE_H_

#include <stdbool.h>
#include <stdint.h>

#include "vibe_usage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char id[VIBE_SOURCE_ID_BYTES];
    uint64_t tokens;
} vibe_source_total_t;

typedef struct {
    int32_t date_key;
    uint64_t total_tokens;
    vibe_source_total_t sources[VIBE_SOURCE_SLOTS];
    uint8_t source_count;
    uint32_t bucket_count;
    bool has_any_data;
    bool sources_collapsed;
} vibe_day_candidate_t;

void vibe_day_candidate_init(vibe_day_candidate_t *candidate,
                             int32_t date_key);
vibe_error_t vibe_day_candidate_add(vibe_day_candidate_t *candidate,
                                    const char *source,
                                    uint64_t total_tokens);
uint16_t vibe_basis_points(uint64_t part, uint64_t total);

#ifdef __cplusplus
}
#endif

#endif  // VIBE_AGGREGATE_H_
