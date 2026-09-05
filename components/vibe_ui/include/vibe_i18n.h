#ifndef VIBE_I18N_H_
#define VIBE_I18N_H_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { VIBE_LANG_EN, VIBE_LANG_ZH_HANS, VIBE_LANG_ZH_HANT, VIBE_LANG_JA, VIBE_LANG_COUNT } vibe_language_t;
typedef struct { const char *text[4]; } vibe_message_t;
extern const vibe_message_t vibe_messages[];
extern const size_t vibe_message_count;
const char *vibe_tr(vibe_language_t language, const char *english);
const char *vibe_language_name(vibe_language_t language);
const char *vibe_language_code(vibe_language_t language);
vibe_language_t vibe_language_next(vibe_language_t language);
/* Strict, bounded UTF-8 decoding; malformed bytes consume one byte as '?'. */
uint32_t vibe_utf8_next(const char **cursor);
extern const uint32_t vibe_glyph_codepoints[];
extern const size_t vibe_glyph_count;
#define VIBE_NOTE_GLYPH_HEIGHT 20
#define VIBE_NOTE_GLYPH_WIDTH 20
extern const uint32_t vibe_glyph_rows[][VIBE_NOTE_GLYPH_HEIGHT];
extern const uint8_t vibe_glyph_advances[];
int vibe_glyph_index(uint32_t codepoint);
#ifdef __cplusplus
}
#endif
#endif
