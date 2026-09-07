#ifndef NOTE_TEXT_CLIP_H_
#define NOTE_TEXT_CLIP_H_
#include <cstring>
#include "vibe_i18n.h"
#include "zectrix_canvas.h"
/* Fit a one-line dynamic title without splitting UTF-8 or drawing past a row. */
static inline void note_clip_text(const ZectrixCanvas& canvas, const char* text,
                                  char* out, size_t size, int width) {
    if (!out || !size) return;
    out[0] = '\0';
    if (!text || size < 4) return;
    if (std::strlen(text) < size && canvas.TextWidth(text) <= width) {
        std::strcpy(out, text);
        return;
    }
    const int ellipsis = canvas.TextWidth("...");
    size_t used = 0;
    const char* cursor = text;
    while (*cursor) {
        const char* start = cursor;
        vibe_utf8_next(&cursor);
        const size_t bytes = static_cast<size_t>(cursor - start);
        if (used + bytes + 4 > size) break;
        std::memcpy(out + used, start, bytes);
        out[used + bytes] = '\0';
        if (canvas.TextWidth(out) > width - ellipsis) {
            out[used] = '\0';
            break;
        }
        used += bytes;
    }
    if (ellipsis <= width) std::strcat(out, "...");
}
#endif
