#include "vibe_aggregate.h"

#include <limits.h>
#include <string.h>

void vibe_day_candidate_init(vibe_day_candidate_t *candidate,
                             int32_t date_key) {
    if (candidate == NULL) return;
    memset(candidate, 0, sizeof(*candidate));
    candidate->date_key = date_key;
}

static size_t source_length_bounded(const char *source) {
    size_t length = 0;
    while (source != NULL && source[length] != '\0' &&
           length < VIBE_SOURCE_ID_BYTES) {
        ++length;
    }
    return length;
}

vibe_error_t vibe_day_candidate_add(vibe_day_candidate_t *candidate,
                                    const char *source,
                                    uint64_t total_tokens) {
    if (candidate == NULL || source == NULL || source[0] == '\0') {
        return VIBE_ERR_INVALID_ARGUMENT;
    }
    if (UINT64_MAX - candidate->total_tokens < total_tokens) {
        return VIBE_ERR_OVERFLOW;
    }

    const size_t length = source_length_bounded(source);
    const bool too_long = length >= VIBE_SOURCE_ID_BYTES;
    const char *normalized = too_long ? VIBE_OTHER_SOURCE_ID : source;
    if (too_long) candidate->sources_collapsed = true;

    size_t index = candidate->source_count;
    for (size_t i = 0; i < candidate->source_count; ++i) {
        if (strcmp(candidate->sources[i].id, normalized) == 0) {
            index = i;
            break;
        }
    }

    if (index == candidate->source_count) {
        const bool is_other = strcmp(normalized, VIBE_OTHER_SOURCE_ID) == 0;
        if (!is_other && candidate->source_count >= VIBE_SOURCE_SLOTS - 1U) {
            normalized = VIBE_OTHER_SOURCE_ID;
            candidate->sources_collapsed = true;
            for (size_t i = 0; i < candidate->source_count; ++i) {
                if (strcmp(candidate->sources[i].id, normalized) == 0) {
                    index = i;
                    break;
                }
            }
        }
        if (index == candidate->source_count) {
            if (candidate->source_count >= VIBE_SOURCE_SLOTS) {
                return VIBE_ERR_OVERFLOW;
            }
            strncpy(candidate->sources[index].id, normalized,
                    VIBE_SOURCE_ID_BYTES - 1U);
            candidate->sources[index].id[VIBE_SOURCE_ID_BYTES - 1U] = '\0';
            ++candidate->source_count;
        }
    }

    if (UINT64_MAX - candidate->sources[index].tokens < total_tokens) {
        return VIBE_ERR_OVERFLOW;
    }
    candidate->sources[index].tokens += total_tokens;
    candidate->total_tokens += total_tokens;
    ++candidate->bucket_count;
    return VIBE_OK;
}

uint16_t vibe_basis_points(uint64_t part, uint64_t total) {
    if (total == 0 || part == 0) return 0;
    if (part >= total) return 10000;
#if defined(__SIZEOF_INT128__)
    const __uint128_t scaled = (__uint128_t)part * 10000U;
    return (uint16_t)(scaled / total);
#else
    /* Safe quotient/remainder multiplication for toolchains without u128. */
    uint64_t remainder = 0;
    uint16_t quotient = 0;
    for (unsigned i = 0; i < 10000U; ++i) {
        if (remainder >= total - part) {
            remainder -= total - part;
            ++quotient;
        } else {
            remainder += part;
        }
    }
    return quotient;
#endif
}
