#include "vibe_i18n.h"
#include <strings.h>

const char *vibe_tr(vibe_language_t language, const char *english) {
    if (!english) return "";
    if (language <= VIBE_LANG_EN || language >= VIBE_LANG_COUNT) return english;
    for (size_t i = 0; i < vibe_message_count; ++i)
        if (strcasecmp(english, vibe_messages[i].text[0]) == 0)
            return vibe_messages[i].text[language];
    return english; /* Identifiers, URLs, metrics and diagnostics are unchanged. */
}
const char *vibe_language_name(vibe_language_t language) {
    static const char *const names[] = {"English", "简体中文", "繁體中文", "日本語"};
    return names[language >= 0 && language < VIBE_LANG_COUNT ? language : VIBE_LANG_EN];
}
const char *vibe_language_code(vibe_language_t language) {
    static const char *const codes[] = {"en-US", "zh-CN", "zh-TW", "ja-JP"};
    return codes[language >= 0 && language < VIBE_LANG_COUNT ? language : VIBE_LANG_EN];
}
vibe_language_t vibe_language_next(vibe_language_t language) {
    return language >= 0 && language < VIBE_LANG_COUNT ? (vibe_language_t)((language + 1) % VIBE_LANG_COUNT) : VIBE_LANG_EN;
}
uint32_t vibe_utf8_next(const char **cursor) {
    const unsigned char *p = (const unsigned char *)*cursor;
    if (!*p) return 0;
    if (*p < 128) { ++*cursor; return *p; }
    unsigned n = (*p >= 0xc2 && *p <= 0xdf) ? 2 : (*p >= 0xe0 && *p <= 0xef) ? 3 : (*p >= 0xf0 && *p <= 0xf4) ? 4 : 0;
    uint32_t cp = n ? *p & ((1U << (7 - n)) - 1) : 0;
    if (n) for (unsigned i = 1; i < n; ++i) {
        if (!p[i] || (p[i] & 0xc0) != 0x80) { n = 0; break; }
        cp = (cp << 6) | (p[i] & 0x3f);
    }
    if (!n || (n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
        (n == 4 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        ++*cursor; return '?';
    }
    *cursor += n;
    return cp;
}
int vibe_glyph_index(uint32_t cp) {
    size_t lo = 0, hi = vibe_glyph_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vibe_glyph_codepoints[mid] < cp) lo = mid + 1;
        else hi = mid;
    }
    return lo < vibe_glyph_count && vibe_glyph_codepoints[lo] == cp ? (int)lo : -1;
}
