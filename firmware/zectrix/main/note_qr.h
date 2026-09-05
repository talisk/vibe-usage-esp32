#ifndef VIBE_NOTE_QR_H_
#define VIBE_NOTE_QR_H_
#include "qrcode.h"
#include "zectrix_canvas.h"

// Shared by the device callback and host pixel/decode regression.
static inline bool note_draw_qr(ZectrixCanvas &canvas, esp_qrcode_handle_t qr,
                               int x, int y, int extent) {
    constexpr int quiet = 4;
    const int modules = esp_qrcode_get_size(qr);
    if (modules <= 0) return false;
    const int scale = extent / (modules + quiet * 2);
    if (scale <= 0) return false;
    const int total = (modules + quiet * 2) * scale;
    const int origin_x = x + (extent - total) / 2;
    const int origin_y = y + (extent - total) / 2;
    canvas.FillRect(x, y, extent, extent, false);
    for (int row = 0; row < modules; ++row)
        for (int col = 0; col < modules; ++col)
            if (esp_qrcode_get_module(qr, col, row))
                canvas.FillRect(origin_x + (col + quiet) * scale,
                                origin_y + (row + quiet) * scale, scale, scale, true);
    return true;
}
#endif
