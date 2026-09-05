#include "zectrix_canvas.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "vibe_i18n.h"

void ZectrixCanvas::Clear(bool white) {
    pixels_.fill(white ? 0xff : 0x00);
}

void ZectrixCanvas::Pixel(int x, int y, bool black) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
        return;
    }
    uint8_t& byte = pixels_[static_cast<size_t>(y) * kStride + x / 8];
    const uint8_t mask = static_cast<uint8_t>(1U << (7 - (x & 7)));
    if (black) {
        byte &= static_cast<uint8_t>(~mask);
    } else {
        byte |= mask;
    }
}

void ZectrixCanvas::FillRect(int x, int y, int width, int height,
                             bool black) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(kWidth, x + width);
    const int bottom = std::min(kHeight, y + height);
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            Pixel(px, py, black);
        }
    }
}

void ZectrixCanvas::Rect(int x, int y, int width, int height, bool black) {
    Line(x, y, x + width - 1, y, black);
    Line(x, y + height - 1, x + width - 1, y + height - 1, black);
    Line(x, y, x, y + height - 1, black);
    Line(x + width - 1, y, x + width - 1, y + height - 1, black);
}

void ZectrixCanvas::Line(int x0, int y0, int x1, int y1, bool black) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        Pixel(x0, y0, black);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void ZectrixCanvas::Text(int x, int y, const char* text, int scale,
                         bool inverted) {
    if (text == nullptr || scale <= 0) {
        return;
    }
    int cursor_x = x;
    const char *cursor = text;
    while (*cursor) {
        uint32_t code = vibe_utf8_next(&cursor);
        int glyph_index = vibe_glyph_index(code);
        if (glyph_index < 0) glyph_index = vibe_glyph_index('?');
        const uint32_t* glyph = vibe_glyph_rows[glyph_index];
        const int glyph_width = vibe_glyph_advances[glyph_index];
        if (inverted) {
            FillRect(cursor_x, y, glyph_width * scale, VIBE_NOTE_GLYPH_HEIGHT * scale, true);
        }
        for (int row = 0; row < VIBE_NOTE_GLYPH_HEIGHT; ++row) {
            for (int column = 0; column < glyph_width; ++column) {
                const bool set =
                    (glyph[row] & (1U << (VIBE_NOTE_GLYPH_WIDTH - 1 - column))) != 0;
                if (!set && !inverted) {
                    continue;
                }
                FillRect(cursor_x + column * scale, y + row * scale,
                         scale, scale, inverted ? !set : true);
            }
        }
        cursor_x += glyph_width * scale;
    }
}

void ZectrixCanvas::TextCentered(int y, const char* text, int scale,
                                 bool inverted) {
    if (scale > 1 && TextWidth(text, scale) > kWidth - 32) scale = 1;
    if (TextWidth(text, scale) > kWidth - 32) {
        /* Bounded two-line paragraphs, split only at UTF-8 codepoint boundaries. */
        const char *cursor = text;
        for (int row = 0; row < 2 && *cursor; ++row) {
            char line[256] = {};
            size_t used = 0;
            while (*cursor) {
                const char *next = cursor;
                vibe_utf8_next(&next);
                const size_t bytes = next - cursor;
                if (used + bytes >= sizeof(line)) break;
                std::memcpy(line + used, cursor, bytes);
                line[used + bytes] = '\0';
                if (TextWidth(line, scale) > kWidth - 32) { line[used] = '\0'; break; }
                used += bytes;
                cursor = next;
            }
            Text((kWidth - TextWidth(line, scale)) / 2, y + row * VIBE_NOTE_GLYPH_HEIGHT * scale, line, scale, inverted);
        }
        return;
    }
    Text((kWidth - TextWidth(text, scale)) / 2, y, text, scale, inverted);
}

int ZectrixCanvas::TextWidth(const char* text, int scale) const {
    if (text == nullptr || scale <= 0) {
        return 0;
    }

    int width = 0;
    const char *cursor = text;
    while (*cursor) {
        uint32_t code = vibe_utf8_next(&cursor);
        int index = vibe_glyph_index(code);
        if (index < 0) index = vibe_glyph_index('?');
        width += vibe_glyph_advances[index] * scale;
    }
    return width;
}
