#ifndef PASSPORT_SURFACE_H_
#define PASSPORT_SURFACE_H_

#include "passport_label.h"

/* Black root remains behind the clipped 28px-radius product surface. */
static inline lv_obj_t *passport_surface_create(lv_obj_t *root) {
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *surface = lv_obj_create(root);
    lv_obj_remove_style_all(surface);
    lv_obj_set_size(surface, 240, 320);
    lv_obj_set_pos(surface, 0, 0);
    lv_obj_set_style_bg_color(surface, lv_color_hex(0xFFF9EE), 0);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(surface, 28, 0);
    lv_obj_set_style_clip_corner(surface, true, 0);
    lv_obj_remove_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
    return surface;
}

/* Vector arrows do not depend on optional Unicode glyph coverage. */
static inline void passport_footer_arrow(lv_obj_t *parent, bool down) {
    static const lv_point_precise_t up[] = {{0, 5}, {5, 0}, {10, 5}, {5, 0}, {5, 13}};
    static const lv_point_precise_t dn[] = {{0, 8}, {5, 13}, {10, 8}, {5, 13}, {5, 0}};
    lv_obj_t *arrow = lv_line_create(parent);
    lv_line_set_points(arrow, down ? dn : up, 5);
    lv_obj_set_pos(arrow, down ? 202 : 28, 294);
    lv_obj_set_style_line_width(arrow, 2, 0);
    lv_obj_set_style_line_color(arrow, lv_color_hex(0x50616A), 0);
    lv_obj_set_style_line_rounded(arrow, true, 0);
}

#endif
