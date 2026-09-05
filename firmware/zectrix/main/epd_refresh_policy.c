#include "epd_refresh_policy.h"

#include <limits.h>
#include <string.h>

bool note4_epd_plan_refresh(const uint8_t *current, const uint8_t *previous,
                            size_t frame_size, uint16_t width,
                            uint16_t height, size_t stride,
                            bool previous_valid, bool force_full,
                            bool layout_changed, bool qr_changed,
                            uint8_t partial_count,
                            note4_epd_refresh_plan_t *plan) {
    if (current == NULL || previous == NULL || plan == NULL || width == 0U ||
        height == 0U) {
        return false;
    }
    const size_t visible_row_bytes = ((size_t)width + 7U) / 8U;
    if (stride < visible_row_bytes ||
        (size_t)height > SIZE_MAX / stride ||
        (size_t)height * stride > frame_size) {
        return false;
    }
    const uint64_t full_area = (uint64_t)width * (uint64_t)height;
    if (full_area > UINT32_MAX) return false;

    memset(plan, 0, sizeof(*plan));
    size_t min_byte = visible_row_bytes;
    size_t max_byte = 0U;
    uint16_t min_y = height;
    uint16_t max_y = 0U;
    bool changed = false;
    for (uint16_t y = 0; y < height; ++y) {
        const size_t row = (size_t)y * stride;
        for (size_t byte = 0; byte < visible_row_bytes; ++byte) {
            if (current[row + byte] == previous[row + byte]) continue;
            if (!changed || byte < min_byte) min_byte = byte;
            if (!changed || byte > max_byte) max_byte = byte;
            if (!changed || y < min_y) min_y = y;
            if (!changed || y > max_y) max_y = y;
            changed = true;
        }
    }

    const bool semantic_full = force_full || !previous_valid ||
                               layout_changed || qr_changed;
    if (!changed) {
        plan->action = semantic_full ? NOTE4_EPD_REFRESH_FULL
                                     : NOTE4_EPD_REFRESH_SKIP;
        if (plan->action == NOTE4_EPD_REFRESH_FULL) {
            plan->width = width;
            plan->height = height;
            plan->row_bytes = visible_row_bytes;
            plan->changed_area = (uint32_t)full_area;
        }
        return true;
    }

    const size_t left = min_byte * 8U;
    size_t right = (max_byte + 1U) * 8U;
    if (right > width) right = width;
    const size_t rect_width = right - left;
    const size_t rect_height = (size_t)max_y - min_y + 1U;
    const uint64_t changed_area = rect_width * rect_height;
    if (left > UINT16_MAX || rect_width > UINT16_MAX ||
        rect_height > UINT16_MAX || changed_area > UINT32_MAX) {
        return false;
    }

    plan->x = (uint16_t)left;
    plan->y = min_y;
    plan->width = (uint16_t)rect_width;
    plan->height = (uint16_t)rect_height;
    plan->row_bytes = max_byte - min_byte + 1U;
    plan->changed_area = (uint32_t)changed_area;
    plan->action = semantic_full || partial_count >= 10U ||
                           changed_area * 2U > full_area
                       ? NOTE4_EPD_REFRESH_FULL
                       : NOTE4_EPD_REFRESH_PARTIAL;
    return true;
}
