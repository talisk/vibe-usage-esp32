#ifndef VIBE_TODO_FONT_H_
#define VIBE_TODO_FONT_H_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* GB2312 + Big5 + Shift-JIS repertoire, generated from locked Noto CJK. */
extern const uint32_t vibe_todo_codepoints[];
extern const size_t vibe_todo_glyph_count;
extern const uint32_t vibe_todo_glyph_rows[][20];
extern const uint8_t vibe_todo_glyph_advances[];
static inline int vibe_todo_glyph_index(uint32_t codepoint) {
    size_t low = 0, high = vibe_todo_glyph_count;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        if (vibe_todo_codepoints[mid] < codepoint) low = mid + 1;
        else high = mid;
    }
    return low < vibe_todo_glyph_count && vibe_todo_codepoints[low] == codepoint ? (int)low : -1;
}
#ifdef __cplusplus
}
#endif
#endif
