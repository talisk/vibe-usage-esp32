#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "vibe_about.h"
#include "zectrix_canvas.h"
#include "vibe_i18n.h"

int main() {
    ZectrixCanvas canvas;
    assert(canvas.TextWidth(VIBE_NOTE_ABOUT, 2) <= 352);
    assert(canvas.TextWidth("Version 0.0.1") <= 352);
    assert(canvas.TextWidth(VIBE_ACCOUNT_NAME) <= 352);
    assert(canvas.TextWidth("VIBE NOTE / OVERVIEW") <= 260);
    for (int lang = 0; lang < VIBE_LANG_COUNT; ++lang) {
        auto language = static_cast<vibe_language_t>(lang);
        for (const char *key : {"Portal closes", "after 10 min", "3. Save Wi-Fi",
                               "1. Connect to", "2. Open", "Scan, then enter:",
                               "No password", "on this device."}) {
            const char *text = vibe_tr(language, key);
            int width = canvas.TextWidth(text);
            if (width > 146) std::fprintf(stderr, "%s: %d > 146\n", text, width);
            assert(width <= 146);
        }
        assert(canvas.TextWidth(vibe_language_name(language)) <= 176);
        for (const char *key : {"WI-FI SETUP", "LINK VIBE ACCOUNT"})
            assert(canvas.TextWidth(vibe_tr(language, key), 2) <= 372);
        assert(canvas.TextWidth(vibe_tr(language, "Repository"), 2) <= 372);
        assert(canvas.TextWidth(vibe_tr(language, "Scan to visit.")) <= 372);
        assert(canvas.TextWidth(vibe_tr(language, "UP Repo")) <= 122);
        assert(canvas.TextWidth(vibe_tr(language, "OK back")) <= 140);
        for (const char *key : {"Reset Settings", "Language", "About", "Display / full"})
            assert(canvas.TextWidth(vibe_tr(language, key)) <= 360);
    }
    // ASCII and CJK use the same baseline/font; selection is its exact inverse.
    for (size_t index = 0; index < vibe_glyph_count; ++index) {
        uint32_t cp = vibe_glyph_codepoints[index];
        assert(cp < 0x10000);
        char text[] = {static_cast<char>(0xe0 | (cp >> 12)),
                       static_cast<char>(0x80 | ((cp >> 6) & 63)),
                       static_cast<char>(0x80 | (cp & 63)), 0};
        if (cp < 128) { text[0] = static_cast<char>(cp); text[1] = 0; }
        assert(canvas.TextWidth(text) == vibe_glyph_advances[index]);
        canvas.Clear(); canvas.Text(0, 0, text);
        unsigned char normal[VIBE_NOTE_GLYPH_HEIGHT * 3];
        for (int y = 0; y < VIBE_NOTE_GLYPH_HEIGHT; ++y)
            for (int x = 0; x < 3; ++x) normal[y * 3 + x] = canvas.data()[y * 50 + x];
        canvas.Clear(); canvas.Text(0, 0, text, 1, true);
        for (int y = 0; y < VIBE_NOTE_GLYPH_HEIGHT; ++y)
            for (int x = 0; x < vibe_glyph_advances[index]; ++x) {
                const auto mask = 1U << (7 - x % 8);
                assert((canvas.data()[y * 50 + x / 8] & mask) != (normal[y * 3 + x / 8] & mask));
            }
    }
    for (int current = VIBE_ABOUT_DETAILS; current <= VIBE_ABOUT_AUTHOR; ++current) {
        auto view = static_cast<vibe_about_view_t>(current);
        assert(vibe_about_navigate(view, VIBE_ABOUT_UP) == VIBE_ABOUT_REPOSITORY);
        assert(vibe_about_navigate(view, VIBE_ABOUT_DOWN) == VIBE_ABOUT_AUTHOR);
        assert(vibe_about_navigate(view, VIBE_ABOUT_BACK) == VIBE_ABOUT_DETAILS);
    }
    assert(std::strcmp(vibe_about_url(VIBE_ABOUT_REPOSITORY), VIBE_REPOSITORY_URL) == 0);
    assert(std::strcmp(vibe_about_url(VIBE_ABOUT_AUTHOR), VIBE_AUTHOR_URL) == 0);
    assert(canvas.TextWidth(VIBE_REPOSITORY_URL) <= 368);
    assert(canvas.TextWidth(VIBE_AUTHOR_URL) <= 352);
    if (const char *dir = std::getenv("VIBE_UI_PREVIEW_DIR")) {
        canvas.Clear();
        const char *samples[] = {"立即刷新", "VibeCafe: 已关联", "时区: Asia/Shanghai",
                                "補齊七日資料", "タイムゾーン: Asia/Shanghai", "Language: English"};
        for (int i = 0; i < 6; ++i) canvas.Text(20, 20 + i * 36, samples[i]);
        char path[512]; std::snprintf(path, sizeof(path), "%s/note-font.pgm", dir);
        FILE *out = std::fopen(path, "wb"); assert(out);
        std::fprintf(out, "P5\n400 300\n255\n");
        for (int y = 0; y < 300; ++y)
            for (int x = 0; x < 400; ++x)
                std::fputc((canvas.data()[y * 50 + x / 8] & (1U << (7 - x % 8))) ? 255 : 0, out);
        std::fclose(out);
    }
    std::puts("Actual Note canvas: CJK pixels, inversion and provisioning widths PASS");
}
