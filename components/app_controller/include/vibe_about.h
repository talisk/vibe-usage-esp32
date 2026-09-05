#ifndef VIBE_ABOUT_H_
#define VIBE_ABOUT_H_
#include "vibe_product.h"

typedef enum { VIBE_ABOUT_DETAILS, VIBE_ABOUT_REPOSITORY, VIBE_ABOUT_AUTHOR } vibe_about_view_t;
typedef enum { VIBE_ABOUT_UP, VIBE_ABOUT_DOWN, VIBE_ABOUT_BACK } vibe_about_action_t;
static inline vibe_about_view_t vibe_about_navigate(vibe_about_view_t current,
                                                   vibe_about_action_t action) {
    (void)current;
    if (action == VIBE_ABOUT_UP) return VIBE_ABOUT_REPOSITORY;
    if (action == VIBE_ABOUT_DOWN) return VIBE_ABOUT_AUTHOR;
    return VIBE_ABOUT_DETAILS;
}
static inline const char *vibe_about_url(vibe_about_view_t view) {
    return view == VIBE_ABOUT_REPOSITORY ? VIBE_REPOSITORY_URL :
           view == VIBE_ABOUT_AUTHOR ? VIBE_AUTHOR_URL : "";
}
static inline const char *vibe_about_title(vibe_about_view_t view) {
    return view == VIBE_ABOUT_REPOSITORY ? "Repository" :
           view == VIBE_ABOUT_AUTHOR ? "X" : "About";
}
#endif
