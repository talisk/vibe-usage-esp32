#ifndef PASSPORT_LABEL_H_
#define PASSPORT_LABEL_H_

#include "lvgl.h"
#include "passport_font.h"

/* Shared with host layout tests. Single-line fields must never grow vertically. */
static inline lv_obj_t *passport_label_create(
    lv_obj_t *parent, const char *text, int x, int y, int width,
    const lv_font_t *font, lv_color_t text_color, lv_text_align_t align) {
    font = passport_font(font);
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text == NULL ? "" : text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, lv_font_get_line_height(font));
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, text_color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    return label;
}

#endif
