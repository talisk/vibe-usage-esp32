#ifndef EPD_REFRESH_POLICY_H_
#define EPD_REFRESH_POLICY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NOTE4_EPD_REFRESH_SKIP = 0,
    NOTE4_EPD_REFRESH_FULL,
    NOTE4_EPD_REFRESH_PARTIAL,
} note4_epd_refresh_action_t;

typedef struct {
    note4_epd_refresh_action_t action;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    size_t row_bytes;
    uint32_t changed_area;
} note4_epd_refresh_plan_t;

/*
 * Compare packed 1bpp frames and choose the only safe display operation.
 * Rows may contain padding; visible pixels begin at byte zero and use
 * MSB-first packing. Returned partial rectangles are byte aligned.
 */
bool note4_epd_plan_refresh(const uint8_t *current, const uint8_t *previous,
                            size_t frame_size, uint16_t width,
                            uint16_t height, size_t stride,
                            bool previous_valid, bool force_full,
                            bool layout_changed, bool qr_changed,
                            uint8_t partial_count,
                            note4_epd_refresh_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif  // EPD_REFRESH_POLICY_H_
