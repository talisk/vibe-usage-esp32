#include "note_qr.h"
#include "note_text_clip.h"
#include "vibe_todo_font.h" // First: verify the device header without incidental host includes.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "vibe_about.h"
extern "C" {
#include "qrcodegen.h"
}
int main() {
    ZectrixCanvas todo_canvas;
    const char* tasks[] = {"两小时后提醒我浇花", "毎週金曜日に薬を飲む", "記得繳電費"};
    for (const char* task : tasks) {
        const char* cursor = task;
        while (*cursor) assert(vibe_todo_glyph_index(vibe_utf8_next(&cursor)) >= 0);
        char clipped[97] = {};
        note_clip_text(todo_canvas, task, clipped, sizeof(clipped), 80);
        assert(todo_canvas.TextWidth(clipped) <= 80);
        assert(std::strstr(clipped, "...") != nullptr);
        cursor = clipped;
        while (*cursor) assert(vibe_utf8_next(&cursor) != '?');
        todo_canvas.Clear();
        todo_canvas.Text(10, 10, task);
    }
    char exact[97] = {};
    note_clip_text(todo_canvas, "完成", exact, sizeof(exact), 100);
    assert(std::strcmp(exact, "完成") == 0);

    ZectrixCanvas canvas;
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(15)] = {};
    uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(15)] = {};
    for (auto view : {VIBE_ABOUT_REPOSITORY, VIBE_ABOUT_AUTHOR}) {
        assert(qrcodegen_encodeText(vibe_about_url(view), temp, code, qrcodegen_Ecc_MEDIUM,
                                   qrcodegen_VERSION_MIN, 15, qrcodegen_Mask_AUTO, true));
        canvas.Clear();
        assert(note_draw_qr(canvas, code, 100, 48, 200));
        // Four quiet modules, plus centering margin, must remain white.
        const int modules = qrcodegen_getSize(code);
        const int scale = 200 / (modules + 8);
        assert(scale >= 4);
        for (int y = 48; y < 48 + scale * 4; ++y)
            for (int x = 100; x < 300; ++x)
                assert(canvas.data()[y * 50 + x / 8] & (1U << (7 - x % 8)));
        if (const char *dir = std::getenv("VIBE_UI_PREVIEW_DIR")) {
            char path[512]; std::snprintf(path, sizeof(path), "%s/note-%s-qr.pgm", dir,
                view == VIBE_ABOUT_REPOSITORY ? "repository" : "author");
            FILE *out = std::fopen(path, "wb"); assert(out);
            std::fprintf(out, "P5\n400 300\n255\n");
            for (int y = 0; y < 300; ++y)
                for (int x = 0; x < 400; ++x)
                    std::fputc(canvas.data()[y * 50 + x / 8] & (1U << (7 - x % 8)) ? 255 : 0, out);
            std::fclose(out);
        }
    }
    // The controller reserves 128 bytes including NUL for a private LAN setup URL.
    char llm_url[128] = {};
    std::strcpy(llm_url, "http://192.168.100.100/?token=");
    const size_t prefix = std::strlen(llm_url);
    std::memset(llm_url + prefix, 'a', sizeof(llm_url) - prefix - 1);
    assert(qrcodegen_encodeText(llm_url, temp, code, qrcodegen_Ecc_MEDIUM,
                               qrcodegen_VERSION_MIN, 15, qrcodegen_Mask_AUTO, true));
    canvas.Clear();
    assert(note_draw_qr(canvas, code, 100, 48, 200));
    assert(200 / (qrcodegen_getSize(code) + 8) >= 3);
    std::puts("Note canvas: dynamic TODO glyphs/UTF-8 clipping and QR quiet zones/max 127-byte LAN URL PASS");
}
