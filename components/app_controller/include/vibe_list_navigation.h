#ifndef VIBE_LIST_NAVIGATION_H_
#define VIBE_LIST_NAVIGATION_H_

#include <stdbool.h>
#include <stdint.h>

static inline uint8_t vibe_list_clamp(uint8_t offset, uint8_t count, uint8_t visible) {
    const uint8_t limit = count > visible ? count - visible : 0;
    return offset > limit ? limit : offset;
}

/* False at an edge means the caller may navigate to the neighboring page. */
static inline bool vibe_list_step(uint8_t *offset, uint8_t count,
                                  uint8_t visible, bool down) {
    *offset = vibe_list_clamp(*offset, count, visible);
    if (down && (unsigned)*offset + visible < count) {
        ++*offset;
        return true;
    }
    if (!down && *offset > 0) {
        --*offset;
        return true;
    }
    return false;
}

#endif
