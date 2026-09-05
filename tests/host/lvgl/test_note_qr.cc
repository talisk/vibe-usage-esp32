#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "note_qr.h"
#include "vibe_about.h"
extern "C" {
#include "qrcodegen.h"
}
int main() {
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
    std::puts("Note actual QR encoder/canvas: both URLs, integer scale and quiet zone PASS");
}
