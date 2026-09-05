#ifndef PASSPORT_FONT_H_
#define PASSPORT_FONT_H_
#include "lvgl.h"
LV_FONT_DECLARE(vibe_font_12);
LV_FONT_DECLARE(vibe_font_14);
LV_FONT_DECLARE(vibe_font_20);
static inline const lv_font_t *passport_font(const lv_font_t *font) {
    if (font == &lv_font_montserrat_12) return &vibe_font_12;
    if (font == &lv_font_montserrat_14) return &vibe_font_14;
    if (font == &lv_font_montserrat_20) return &vibe_font_20;
    return font;
}
#endif
