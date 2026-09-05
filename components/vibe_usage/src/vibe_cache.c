#include "vibe_cache.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "vibe_date.h"

#define CACHE_MAGIC_0 'V'
#define CACHE_MAGIC_1 'U'
#define CACHE_MAGIC_2 'C'
#define CACHE_MAGIC_3 '1'

typedef struct {
    uint8_t *data;
    size_t size;
    size_t offset;
} writer_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t offset;
} reader_t;

static bool write_bytes(writer_t *writer, const void *data, size_t size) {
    if (writer == NULL || data == NULL || size > writer->size - writer->offset) {
        return false;
    }
    memcpy(writer->data + writer->offset, data, size);
    writer->offset += size;
    return true;
}

static bool write_u8(writer_t *writer, uint8_t value) {
    return write_bytes(writer, &value, 1);
}

static bool write_u32(writer_t *writer, uint32_t value) {
    uint8_t bytes[4];
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (i * 8U));
    return write_bytes(writer, bytes, sizeof(bytes));
}

static bool write_u64(writer_t *writer, uint64_t value) {
    uint8_t bytes[8];
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (uint8_t)(value >> (i * 8U));
    return write_bytes(writer, bytes, sizeof(bytes));
}

static bool read_bytes(reader_t *reader, void *data, size_t size) {
    if (reader == NULL || data == NULL || size > reader->size - reader->offset) {
        return false;
    }
    memcpy(data, reader->data + reader->offset, size);
    reader->offset += size;
    return true;
}

static bool read_u8(reader_t *reader, uint8_t *value) {
    return read_bytes(reader, value, 1);
}

static bool read_u32(reader_t *reader, uint32_t *value) {
    uint8_t bytes[4];
    if (!read_bytes(reader, bytes, sizeof(bytes))) return false;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
             ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
    return true;
}

static bool read_u64(reader_t *reader, uint64_t *value) {
    uint8_t bytes[8];
    if (!read_bytes(reader, bytes, sizeof(bytes))) return false;
    uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) result |= (uint64_t)bytes[i] << (i * 8U);
    *value = result;
    return true;
}

static size_t bounded_strlen(const char *text, size_t limit) {
    size_t length = 0;
    while (length < limit && text[length] != '\0') ++length;
    return length;
}

static vibe_error_t validate_cache(const vibe_cache_t *cache) {
    if (cache == NULL || cache->schema_version != VIBE_CACHE_SCHEMA_VERSION ||
        cache->config_revision == 0 ||
        cache->metric_id != VIBE_METRIC_ID_API_TOTAL_V1 ||
        (unsigned)cache->timezone > (unsigned)VIBE_TZ_UTC ||
        cache->last_reconcile_at < 0 ||
        (cache->reconcile_pending_mask & (uint8_t)~0x7fU) != 0 ||
        (cache->reconcile_anchor_key != 0 &&
         !vibe_date_is_valid(cache->reconcile_anchor_key)) ||
        (cache->reconcile_pending_mask != 0 &&
         cache->reconcile_anchor_key == 0)) {
        return VIBE_ERR_SCHEMA;
    }

    for (size_t source = 0; source < VIBE_SOURCE_SLOTS; ++source) {
        const size_t length = bounded_strlen(cache->source_ids[source],
                                             VIBE_SOURCE_ID_BYTES);
        if (length >= VIBE_SOURCE_ID_BYTES) return VIBE_ERR_SCHEMA;
        if (source == VIBE_SOURCE_SLOTS - 1U &&
            strcmp(cache->source_ids[source], VIBE_OTHER_SOURCE_ID) != 0) {
            return VIBE_ERR_SCHEMA;
        }
        if (length == 0) continue;
        for (size_t prior = 0; prior < source; ++prior) {
            if (cache->source_ids[prior][0] != '\0' &&
                strcmp(cache->source_ids[prior],
                       cache->source_ids[source]) == 0) {
                return VIBE_ERR_SCHEMA;
            }
        }
    }

    for (size_t day = 0; day < VIBE_CACHE_DAYS; ++day) {
        const vibe_daily_slot_t *slot = &cache->days[day];
        if (!slot->valid) {
            if (slot->date_key != 0 || slot->fetched_at != 0 ||
                slot->total_tokens != 0 || slot->has_any_data ||
                slot->sources_collapsed) {
                return VIBE_ERR_SCHEMA;
            }
            for (size_t source = 0; source < VIBE_SOURCE_SLOTS; ++source) {
                if (slot->source_tokens[source] != 0) return VIBE_ERR_SCHEMA;
            }
            continue;
        }
        if (!vibe_date_is_valid(slot->date_key) || slot->fetched_at <= 0) {
            return VIBE_ERR_SCHEMA;
        }
        for (size_t prior = 0; prior < day; ++prior) {
            if (cache->days[prior].valid &&
                cache->days[prior].date_key == slot->date_key) {
                return VIBE_ERR_SCHEMA;
            }
        }
        uint64_t sum = 0;
        for (size_t source = 0; source < VIBE_SOURCE_SLOTS; ++source) {
            const uint64_t tokens = slot->source_tokens[source];
            if ((cache->source_ids[source][0] == '\0' && tokens != 0) ||
                UINT64_MAX - sum < tokens) {
                return VIBE_ERR_SCHEMA;
            }
            sum += tokens;
        }
        if (sum != slot->total_tokens) return VIBE_ERR_SCHEMA;
    }
    return VIBE_OK;
}

uint32_t vibe_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = UINT32_MAX;
    if (data == NULL && size != 0) return 0;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void vibe_cache_init(vibe_cache_t *cache, uint32_t generation,
                     uint32_t config_revision, vibe_timezone_t timezone) {
    if (cache == NULL) return;
    memset(cache, 0, sizeof(*cache));
    cache->schema_version = VIBE_CACHE_SCHEMA_VERSION;
    cache->generation = generation;
    cache->config_revision = config_revision;
    cache->metric_id = VIBE_METRIC_ID_API_TOTAL_V1;
    cache->timezone = timezone;
    strncpy(cache->source_ids[VIBE_SOURCE_SLOTS - 1U], VIBE_OTHER_SOURCE_ID,
            VIBE_SOURCE_ID_BYTES - 1U);
}

const vibe_daily_slot_t *vibe_cache_find_day(const vibe_cache_t *cache,
                                             int32_t date_key) {
    if (cache == NULL) return NULL;
    for (size_t i = 0; i < VIBE_CACHE_DAYS; ++i) {
        if (cache->days[i].valid && cache->days[i].date_key == date_key) {
            return &cache->days[i];
        }
    }
    return NULL;
}

static vibe_daily_slot_t *choose_slot(vibe_cache_t *cache, int32_t date_key) {
    vibe_daily_slot_t *oldest = &cache->days[0];
    for (size_t i = 0; i < VIBE_CACHE_DAYS; ++i) {
        vibe_daily_slot_t *slot = &cache->days[i];
        if (slot->valid && slot->date_key == date_key) return slot;
        if (!slot->valid) return slot;
        if (slot->date_key < oldest->date_key) oldest = slot;
    }
    return oldest;
}

static bool same_day_value(const vibe_daily_slot_t *left,
                           const vibe_daily_slot_t *right) {
    return left->valid == right->valid &&
           left->date_key == right->date_key &&
           left->total_tokens == right->total_tokens &&
           left->has_any_data == right->has_any_data &&
           left->sources_collapsed == right->sources_collapsed &&
           memcmp(left->source_tokens, right->source_tokens,
                  sizeof(left->source_tokens)) == 0;
}

static size_t dictionary_index(vibe_cache_t *cache, const char *source,
                               bool *collapsed) {
    if (strcmp(source, VIBE_OTHER_SOURCE_ID) == 0) {
        return VIBE_SOURCE_SLOTS - 1U;
    }
    for (size_t i = 0; i < VIBE_SOURCE_SLOTS - 1U; ++i) {
        if (strcmp(cache->source_ids[i], source) == 0) return i;
    }
    for (size_t i = 0; i < VIBE_SOURCE_SLOTS - 1U; ++i) {
        if (cache->source_ids[i][0] == '\0') {
            strncpy(cache->source_ids[i], source, VIBE_SOURCE_ID_BYTES - 1U);
            cache->source_ids[i][VIBE_SOURCE_ID_BYTES - 1U] = '\0';
            return i;
        }
    }
    *collapsed = true;
    return VIBE_SOURCE_SLOTS - 1U;
}

vibe_error_t vibe_cache_replace_day(vibe_cache_t *cache,
                                    const vibe_day_candidate_t *candidate,
                                    int64_t fetched_at) {
    if (cache == NULL || candidate == NULL ||
        !vibe_date_is_valid(candidate->date_key) ||
        fetched_at <= 0 || candidate->source_count > VIBE_SOURCE_SLOTS ||
        cache->schema_version != VIBE_CACHE_SCHEMA_VERSION ||
        cache->metric_id != VIBE_METRIC_ID_API_TOTAL_V1) {
        return VIBE_ERR_INVALID_ARGUMENT;
    }

    uint64_t source_sum = 0;
    for (size_t i = 0; i < candidate->source_count; ++i) {
        const size_t length = bounded_strlen(candidate->sources[i].id,
                                             VIBE_SOURCE_ID_BYTES);
        if (length == 0 || length >= VIBE_SOURCE_ID_BYTES) {
            return VIBE_ERR_SCHEMA;
        }
        if (UINT64_MAX - source_sum < candidate->sources[i].tokens) {
            return VIBE_ERR_OVERFLOW;
        }
        source_sum += candidate->sources[i].tokens;
    }
    if (source_sum != candidate->total_tokens) return VIBE_ERR_SCHEMA;

    vibe_daily_slot_t next;
    memset(&next, 0, sizeof(next));
    next.date_key = candidate->date_key;
    next.fetched_at = fetched_at;
    next.total_tokens = candidate->total_tokens;
    next.valid = true;
    next.has_any_data = candidate->has_any_data;
    next.sources_collapsed = candidate->sources_collapsed;

    for (size_t i = 0; i < candidate->source_count; ++i) {
        const size_t index = dictionary_index(cache, candidate->sources[i].id,
                                              &next.sources_collapsed);
        if (UINT64_MAX - next.source_tokens[index] <
            candidate->sources[i].tokens) {
            return VIBE_ERR_OVERFLOW;
        }
        next.source_tokens[index] += candidate->sources[i].tokens;
    }

    vibe_daily_slot_t *slot = choose_slot(cache, candidate->date_key);
    const bool changed = !same_day_value(slot, &next);
    *slot = next;
    if (changed) {
        ++cache->sequence;
        cache->persisted = false;
    }
    return VIBE_OK;
}

typedef struct {
    size_t dictionary_index;
    uint64_t today;
    uint64_t seven;
    uint64_t selected;
} ranked_source_t;

static bool source_precedes(const vibe_cache_t *cache,
                            const ranked_source_t *left,
                            const ranked_source_t *right) {
    if (left->selected != right->selected) return left->selected > right->selected;
    return strcmp(cache->source_ids[left->dictionary_index],
                  cache->source_ids[right->dictionary_index]) < 0;
}

vibe_error_t vibe_cache_build_snapshot(const vibe_cache_t *cache,
                                       int32_t today_key,
                                       vibe_window_t selected_window,
                                       vibe_usage_snapshot_t *snapshot) {
    if (cache == NULL || snapshot == NULL || !vibe_date_is_valid(today_key) ||
        (selected_window != VIBE_WINDOW_TODAY &&
         selected_window != VIBE_WINDOW_SEVEN_DAYS)) {
        return VIBE_ERR_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->schema_version = VIBE_SCHEMA_VERSION;
    snapshot->generation = cache->generation;
    snapshot->today_key = today_key;
    snapshot->time_valid = true;
    snapshot->persisted = cache->persisted;
    snapshot->last_reconcile_at = cache->last_reconcile_at;

    uint64_t today_by_source[VIBE_SOURCE_SLOTS] = {0};
    uint64_t seven_by_source[VIBE_SOURCE_SLOTS] = {0};
    bool collapsed = false;

    for (unsigned age = 0; age < 7U; ++age) {
        int32_t key;
        vibe_error_t error = vibe_date_add_days(today_key, -(int)age, &key);
        if (error != VIBE_OK) return error;
        const vibe_daily_slot_t *slot = vibe_cache_find_day(cache, key);
        if (slot == NULL) continue;
        snapshot->valid_days_mask |= (uint8_t)(1U << age);
        snapshot->has_lkg = true;
        collapsed = collapsed || slot->sources_collapsed;
        if (UINT64_MAX - snapshot->seven_day_tokens < slot->total_tokens) {
            return VIBE_ERR_OVERFLOW;
        }
        snapshot->seven_day_tokens += slot->total_tokens;
        for (size_t source = 0; source < VIBE_SOURCE_SLOTS; ++source) {
            if (UINT64_MAX - seven_by_source[source] <
                slot->source_tokens[source]) {
                return VIBE_ERR_OVERFLOW;
            }
            seven_by_source[source] += slot->source_tokens[source];
            if (age == 0) today_by_source[source] = slot->source_tokens[source];
        }
        if (age == 0) {
            snapshot->today_tokens = slot->total_tokens;
            snapshot->today_complete = true;
            snapshot->has_any_data = slot->has_any_data;
            snapshot->last_fetch_at = slot->fetched_at;
        }
    }

    if (!snapshot->has_lkg) {
        for (size_t i = 0; i < VIBE_CACHE_DAYS; ++i) {
            if (cache->days[i].valid) {
                snapshot->has_lkg = true;
                break;
            }
        }
    }
    snapshot->seven_day_complete =
        (snapshot->valid_days_mask & 0x7fU) == 0x7fU;
    const bool complete = selected_window == VIBE_WINDOW_TODAY
                              ? snapshot->today_complete
                              : snapshot->seven_day_complete;
    const uint64_t selected_tokens = selected_window == VIBE_WINDOW_TODAY
                                         ? snapshot->today_tokens
                                         : snapshot->seven_day_tokens;
    snapshot->state = !complete ? VIBE_STALE
                                : (selected_tokens == 0 ? VIBE_EMPTY : VIBE_READY);

    ranked_source_t ranked[VIBE_SOURCE_SLOTS - 1U];
    size_t ranked_count = 0;
    for (size_t source = 0; source < VIBE_SOURCE_SLOTS - 1U; ++source) {
        if (cache->source_ids[source][0] == '\0' ||
            (today_by_source[source] == 0 && seven_by_source[source] == 0)) {
            continue;
        }
        ranked_source_t item = {
            .dictionary_index = source,
            .today = today_by_source[source],
            .seven = seven_by_source[source],
            .selected = selected_window == VIBE_WINDOW_TODAY
                            ? today_by_source[source]
                            : seven_by_source[source],
        };
        size_t insert = ranked_count;
        while (insert > 0 && source_precedes(cache, &item, &ranked[insert - 1U])) {
            ranked[insert] = ranked[insert - 1U];
            --insert;
        }
        ranked[insert] = item;
        ++ranked_count;
    }

    const size_t visible = ranked_count < VIBE_MAX_AGENTS - 1U
                               ? ranked_count
                               : VIBE_MAX_AGENTS - 1U;
    for (size_t i = 0; i < visible; ++i) {
        vibe_agent_usage_t *agent = &snapshot->agents[snapshot->agent_count++];
        strncpy(agent->id, cache->source_ids[ranked[i].dictionary_index],
                VIBE_SOURCE_ID_BYTES - 1U);
        agent->today_tokens = ranked[i].today;
        agent->seven_day_tokens = ranked[i].seven;
        agent->today_bp = vibe_basis_points(agent->today_tokens,
                                            snapshot->today_tokens);
        agent->seven_day_bp = vibe_basis_points(agent->seven_day_tokens,
                                                snapshot->seven_day_tokens);
    }

    uint64_t other_today = today_by_source[VIBE_SOURCE_SLOTS - 1U];
    uint64_t other_seven = seven_by_source[VIBE_SOURCE_SLOTS - 1U];
    for (size_t i = visible; i < ranked_count; ++i) {
        if (UINT64_MAX - other_today < ranked[i].today ||
            UINT64_MAX - other_seven < ranked[i].seven) {
            return VIBE_ERR_OVERFLOW;
        }
        other_today += ranked[i].today;
        other_seven += ranked[i].seven;
        collapsed = true;
    }
    if ((other_today != 0 || other_seven != 0) &&
        snapshot->agent_count < VIBE_MAX_AGENTS) {
        vibe_agent_usage_t *other = &snapshot->agents[snapshot->agent_count++];
        strncpy(other->id, VIBE_OTHER_SOURCE_ID, VIBE_SOURCE_ID_BYTES - 1U);
        other->today_tokens = other_today;
        other->seven_day_tokens = other_seven;
        other->today_bp = vibe_basis_points(other_today, snapshot->today_tokens);
        other->seven_day_bp = vibe_basis_points(other_seven,
                                                snapshot->seven_day_tokens);
    }
    snapshot->sources_collapsed = collapsed;
    return VIBE_OK;
}

vibe_error_t vibe_cache_encode(const vibe_cache_t *cache, uint8_t *output,
                               size_t output_size, size_t *written) {
    if (cache == NULL || output == NULL || written == NULL ||
        output_size > VIBE_CACHE_BLOB_MAX_BYTES || output_size < 64) {
        return VIBE_ERR_INVALID_ARGUMENT;
    }
    const vibe_error_t validation = validate_cache(cache);
    if (validation != VIBE_OK) return validation;
    writer_t writer = {.data = output, .size = output_size, .offset = 0};
    const uint8_t magic[] = {CACHE_MAGIC_0, CACHE_MAGIC_1, CACHE_MAGIC_2,
                             CACHE_MAGIC_3};
    if (!write_bytes(&writer, magic, sizeof(magic)) ||
        !write_u32(&writer, cache->schema_version) ||
        !write_u32(&writer, 0) ||
        !write_u32(&writer, cache->sequence) ||
        !write_u32(&writer, cache->generation) ||
        !write_u32(&writer, cache->config_revision) ||
        !write_u32(&writer, cache->metric_id) ||
        !write_u8(&writer, (uint8_t)cache->timezone) ||
        !write_u64(&writer, (uint64_t)cache->last_reconcile_at) ||
        !write_u32(&writer, (uint32_t)cache->reconcile_anchor_key) ||
        !write_u8(&writer, cache->reconcile_pending_mask)) {
        return VIBE_ERR_BUFFER_TOO_SMALL;
    }

    for (size_t i = 0; i < VIBE_SOURCE_SLOTS; ++i) {
        const size_t length = bounded_strlen(cache->source_ids[i],
                                             VIBE_SOURCE_ID_BYTES);
        if (length >= VIBE_SOURCE_ID_BYTES || !write_u8(&writer, (uint8_t)length) ||
            (length != 0 && !write_bytes(&writer, cache->source_ids[i], length))) {
            return length >= VIBE_SOURCE_ID_BYTES ? VIBE_ERR_SCHEMA
                                                   : VIBE_ERR_BUFFER_TOO_SMALL;
        }
    }
    for (size_t day = 0; day < VIBE_CACHE_DAYS; ++day) {
        const vibe_daily_slot_t *slot = &cache->days[day];
        const uint8_t flags = (uint8_t)((slot->valid ? 1U : 0U) |
                                        (slot->has_any_data ? 2U : 0U) |
                                        (slot->sources_collapsed ? 4U : 0U));
        if (!write_u8(&writer, flags) ||
            !write_u32(&writer, (uint32_t)slot->date_key) ||
            !write_u64(&writer, (uint64_t)slot->fetched_at) ||
            !write_u64(&writer, slot->total_tokens)) {
            return VIBE_ERR_BUFFER_TOO_SMALL;
        }
        for (size_t source = 0; source < VIBE_SOURCE_SLOTS; ++source) {
            if (!write_u64(&writer, slot->source_tokens[source])) {
                return VIBE_ERR_BUFFER_TOO_SMALL;
            }
        }
    }
    if (writer.offset + 4U > writer.size) return VIBE_ERR_BUFFER_TOO_SMALL;
    const uint32_t total_length = (uint32_t)(writer.offset + 4U);
    output[8] = (uint8_t)total_length;
    output[9] = (uint8_t)(total_length >> 8U);
    output[10] = (uint8_t)(total_length >> 16U);
    output[11] = (uint8_t)(total_length >> 24U);
    const uint32_t crc = vibe_crc32(output, writer.offset);
    if (!write_u32(&writer, crc)) return VIBE_ERR_BUFFER_TOO_SMALL;
    *written = writer.offset;
    return VIBE_OK;
}

static vibe_error_t decode_cache_into(const uint8_t *input, size_t input_size,
                                      vibe_cache_t *decoded) {
    if (input == NULL || decoded == NULL || input_size < 64U ||
        input_size > VIBE_CACHE_BLOB_MAX_BYTES) {
        return VIBE_ERR_INVALID_ARGUMENT;
    }
    const uint32_t expected_crc = (uint32_t)input[input_size - 4U] |
        ((uint32_t)input[input_size - 3U] << 8U) |
        ((uint32_t)input[input_size - 2U] << 16U) |
        ((uint32_t)input[input_size - 1U] << 24U);
    if (vibe_crc32(input, input_size - 4U) != expected_crc) return VIBE_ERR_CRC;

    reader_t reader = {.data = input, .size = input_size - 4U, .offset = 0};
    uint8_t magic[4];
    uint32_t encoded_length;
    memset(decoded, 0, sizeof(*decoded));
    uint8_t timezone;
    uint64_t value64;
    if (!read_bytes(&reader, magic, sizeof(magic)) ||
        memcmp(magic, "VUC1", 4) != 0 ||
        !read_u32(&reader, &decoded->schema_version) ||
        !read_u32(&reader, &encoded_length)) {
        return VIBE_ERR_SCHEMA;
    }
    if (encoded_length != input_size) return VIBE_ERR_SCHEMA;
    if (decoded->schema_version != VIBE_CACHE_SCHEMA_VERSION) return VIBE_ERR_VERSION;
    if (!read_u32(&reader, &decoded->sequence) ||
        !read_u32(&reader, &decoded->generation) ||
        !read_u32(&reader, &decoded->config_revision) ||
        !read_u32(&reader, &decoded->metric_id) || !read_u8(&reader, &timezone) ||
        !read_u64(&reader, &value64)) {
        return VIBE_ERR_SCHEMA;
    }
    if (decoded->metric_id != VIBE_METRIC_ID_API_TOTAL_V1 ||
        timezone > (uint8_t)VIBE_TZ_UTC) {
        return VIBE_ERR_VERSION;
    }
    decoded->timezone = (vibe_timezone_t)timezone;
    if (value64 > (uint64_t)INT64_MAX) return VIBE_ERR_SCHEMA;
    decoded->last_reconcile_at = (int64_t)value64;
    uint32_t value32;
    if (!read_u32(&reader, &value32) ||
        !read_u8(&reader, &decoded->reconcile_pending_mask)) {
        return VIBE_ERR_SCHEMA;
    }
    decoded->reconcile_anchor_key = (int32_t)value32;

    for (size_t i = 0; i < VIBE_SOURCE_SLOTS; ++i) {
        uint8_t length;
        if (!read_u8(&reader, &length) || length >= VIBE_SOURCE_ID_BYTES ||
            !read_bytes(&reader, decoded->source_ids[i], length)) {
            return VIBE_ERR_SCHEMA;
        }
        decoded->source_ids[i][length] = '\0';
    }
    if (strcmp(decoded->source_ids[VIBE_SOURCE_SLOTS - 1U],
               VIBE_OTHER_SOURCE_ID) != 0) {
        return VIBE_ERR_SCHEMA;
    }

    for (size_t day = 0; day < VIBE_CACHE_DAYS; ++day) {
        vibe_daily_slot_t *slot = &decoded->days[day];
        uint8_t flags;
        if (!read_u8(&reader, &flags) || (flags & ~7U) != 0 ||
            !read_u32(&reader, &value32) || !read_u64(&reader, &value64)) {
            return VIBE_ERR_SCHEMA;
        }
        slot->valid = (flags & 1U) != 0;
        slot->has_any_data = (flags & 2U) != 0;
        slot->sources_collapsed = (flags & 4U) != 0;
        slot->date_key = (int32_t)value32;
        if (value64 > (uint64_t)INT64_MAX) return VIBE_ERR_SCHEMA;
        slot->fetched_at = (int64_t)value64;
        if (!read_u64(&reader, &slot->total_tokens)) return VIBE_ERR_SCHEMA;
        uint64_t sum = 0;
        for (size_t source = 0; source < VIBE_SOURCE_SLOTS; ++source) {
            if (!read_u64(&reader, &slot->source_tokens[source]) ||
                UINT64_MAX - sum < slot->source_tokens[source]) {
                return VIBE_ERR_SCHEMA;
            }
            sum += slot->source_tokens[source];
        }
        if (slot->valid && sum != slot->total_tokens) {
            return VIBE_ERR_SCHEMA;
        }
    }
    if (reader.offset != reader.size) return VIBE_ERR_SCHEMA;
    const vibe_error_t validation = validate_cache(decoded);
    if (validation != VIBE_OK) return validation;
    decoded->persisted = true;
    return VIBE_OK;
}

vibe_error_t vibe_cache_decode(const uint8_t *input, size_t input_size,
                               vibe_cache_t *cache) {
    if (cache == NULL) return VIBE_ERR_INVALID_ARGUMENT;
    vibe_cache_t *decoded = (vibe_cache_t *)calloc(1, sizeof(*decoded));
    if (decoded == NULL) return VIBE_ERR_NO_MEMORY;
    const vibe_error_t error = decode_cache_into(input, input_size, decoded);
    if (error == VIBE_OK) *cache = *decoded;
    memset(decoded, 0, sizeof(*decoded));
    free(decoded);
    return error;
}
