#ifndef PASSPORT_TODO_TITLE_H_
#define PASSPORT_TODO_TITLE_H_
#include "passport_label.h"
LV_FONT_DECLARE(vibe_todo_font_12);
/* A two-line task title uses the large dynamic repertoire at one size only. */
static inline lv_obj_t *passport_todo_title_create(lv_obj_t *parent, const char *title,
                                                  lv_color_t ink) {
    lv_obj_t *label = passport_label_create(parent, title, 52, 3, 158,
                                            &vibe_todo_font_12, ink, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_height(label, 33);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}
#endif
