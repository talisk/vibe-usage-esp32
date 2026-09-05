#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "vibe_i18n.h"

int main(void) {
    assert(vibe_language_next(VIBE_LANG_JA) == VIBE_LANG_EN);
    assert(strcmp(vibe_language_code(VIBE_LANG_EN), "en-US") == 0);
    assert(strcmp(vibe_language_name(VIBE_LANG_JA), "日本語") == 0);
    for (size_t i = 0; i < vibe_message_count; ++i) {
        assert(strcmp(vibe_tr(VIBE_LANG_EN, vibe_messages[i].text[0]), vibe_messages[i].text[0]) == 0);
        for (int lang = 1; lang < VIBE_LANG_COUNT; ++lang) {
            const char *s = vibe_tr((vibe_language_t)lang, vibe_messages[i].text[0]);
            assert(strcmp(s, vibe_messages[i].text[lang]) == 0);
            while (*s) {
                uint32_t cp = vibe_utf8_next(&s);
                if (cp >= 128) assert(vibe_glyph_index(cp) >= 0);
            }
        }
    }
    const char *bad[] = {"\xc0\xaf", "\xe3", "\xed\xa0\x80", "\xf4\x90\x80\x80"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        const char *s = bad[i];
        assert(vibe_utf8_next(&s) == '?');
        assert(s == bad[i] + 1);
    }
    assert(vibe_glyph_index(0x10ffff) == -1);
    puts("Four-language lookup, default/cycle, UTF-8 bounds and glyph coverage: PASS");
}
