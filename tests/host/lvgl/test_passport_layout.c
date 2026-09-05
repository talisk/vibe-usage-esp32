#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "passport_label.h"
#include "passport_surface.h"
#include "vibe_product.h"
#include "vibe_i18n.h"

static uint16_t pixels[240 * 320];
static unsigned flushes;
static void save_pixels(const char *name) {
    const char *dir = getenv("VIBE_UI_PREVIEW_DIR");
    if (!dir) return;
    char path[512]; snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *out = fopen(path, "wb"); assert(out);
    fprintf(out, "P6\n240 320\n255\n");
    for (int i = 0; i < 240 * 320; ++i) {
        fputc(((pixels[i] >> 11) & 31) * 255 / 31, out);
        fputc(((pixels[i] >> 5) & 63) * 255 / 63, out);
        fputc((pixels[i] & 31) * 255 / 31, out);
    }
    fclose(out);
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    (void)area;
    (void)data;
    ++flushes;
    lv_display_flush_ready(display);
}

static void check_surface(lv_display_t *display, lv_obj_t *root) {
    lv_obj_t *surface = passport_surface_create(root);
    /* An intentionally overflowing child must not whiten any outer corner. */
    lv_obj_t *fill = lv_obj_create(surface);
    lv_obj_remove_style_all(fill);
    lv_obj_set_size(fill, 240, 320);
    lv_obj_set_style_bg_color(fill, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    passport_footer_arrow(surface, false);
    passport_footer_arrow(surface, true);
    lv_obj_update_layout(root);
    lv_refr_now(display);
    assert(flushes > 0);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            assert(pixels[y * 240 + x] == 0);
            assert(pixels[y * 240 + 239 - x] == 0);
            assert(pixels[(319 - y) * 240 + x] == 0);
            assert(pixels[(319 - y) * 240 + 239 - x] == 0);
        }
    }
    assert(pixels[160 * 240 + 120] == 0xffff);
    for (int side = 0; side < 2; ++side) {
        unsigned ink = 0;
        for (int y = 293; y < 309; ++y)
            for (int x = (side ? 201 : 27); x < (side ? 214 : 40); ++x)
                ink += pixels[y * 240 + x] != 0xffff;
        assert(ink >= 20); /* Both arrows are genuinely rendered inside the panel. */
    }
    lv_obj_clean(root);
}

static void fits(const char *text, int width, const lv_font_t *font) {
    font = passport_font(font);
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x > width) fprintf(stderr, "Text does not fit: %s (%ld > %d)\n",
                                text, (long)size.x, width);
    assert(size.x <= width);
}

int main(void) {
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    lv_obj_t *screen = lv_screen_active();
    const char *values[] = {"10.7M", "999K", "18.4E", "0"};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        fits(values[i], 48, &lv_font_montserrat_12);
    fits("100.0%", 48, &lv_font_montserrat_12);
    fits("WiFi  100%", 72, &lv_font_montserrat_12);
    fits("VIBE / OVERVIEW", 140, &lv_font_montserrat_14);
    fits("Timezone", 76, &lv_font_montserrat_12);
    fits("Asia/Shanghai", 128, &lv_font_montserrat_12);
    fits("Online / saved", 128, &lv_font_montserrat_12);
    fits("100% / 4200mV", 128, &lv_font_montserrat_12);
    fits("OK Today / 7D", 128, &lv_font_montserrat_12);
    fits("Stale / 7/7", 128, &lv_font_montserrat_12);
    fits("Auth required", 128, &lv_font_montserrat_12);
    fits(VIBE_PASSPORT_ABOUT, 216, &lv_font_montserrat_20);
    fits("Version 0.0.1", 216, &lv_font_montserrat_14);
    fits(VIBE_REPOSITORY_OWNER_URL, 208, &lv_font_montserrat_12);
    fits(VIBE_REPOSITORY_NAME, 208, &lv_font_montserrat_12);
    fits(VIBE_AUTHOR_URL, 208, &lv_font_montserrat_12);
    fits(VIBE_ACCOUNT_NAME, 200, &lv_font_montserrat_12);
    const char *compact[] = {"Refresh now", "Reconcile 7 days", "Wi-Fi setup", "Timezone", "Display",
        "About", "Reset Settings", "Language", "OK Today / 7D", "OK / hold back"};
    for (int lang = 0; lang < VIBE_LANG_COUNT; ++lang) {
        for (unsigned i = 0; i < sizeof(compact)/sizeof(compact[0]); ++i)
            fits(vibe_tr((vibe_language_t)lang, compact[i]), i >= 8 ? 128 : 200, &vibe_font_12);
        fits(vibe_language_name((vibe_language_t)lang), 128, &vibe_font_12);
        fits(vibe_tr((vibe_language_t)lang, "UP Repo / DOWN X"), 208, &vibe_font_12);
        fits(vibe_tr((vibe_language_t)lang, "Scan to visit."), 208, &vibe_font_14);
        fits(vibe_tr((vibe_language_t)lang, "Repository"), 208, &vibe_font_20);
        fits(vibe_tr((vibe_language_t)lang, "LAST 7 DAYS"), 90, &vibe_font_14);
        fits(vibe_tr((vibe_language_t)lang, "TOKENS"), 64, &vibe_font_14);
        fits(vibe_tr((vibe_language_t)lang, "VIBE / OVERVIEW"), 140, &vibe_font_14);
        fits(vibe_tr((vibe_language_t)lang, "VIBE / AGENTS"), 140, &vibe_font_14);
        for (const char **key = (const char *[]){"System", "Account", "Timezone", "Last sync", "Metric", "Firmware", "Battery", NULL}; *key; ++key)
            fits(vibe_tr((vibe_language_t)lang, *key), 76, &vibe_font_12);
    }
    for (size_t i = 0; i < vibe_glyph_count; ++i) {
        const lv_font_t *fonts[] = {&vibe_font_12, &vibe_font_14, &vibe_font_20};
        for (unsigned f = 0; f < 3; ++f) {
            lv_font_glyph_dsc_t glyph = {0};
            assert(lv_font_get_glyph_dsc(fonts[f], &glyph, vibe_glyph_codepoints[i], 0));
            assert(!glyph.is_placeholder);
            assert(glyph.resolved_font == fonts[f]);
            assert(glyph.ofs_y == -fonts[f]->base_line);
            assert(glyph.box_h <= fonts[f]->line_height);
        }
    }
    fits("7D coverage 7/7 / Ready", 208, &lv_font_montserrat_12);
    const char *samples[] = {"10.7M", "A very long synthetic source name", "first\nsecond"};
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        lv_obj_t *label = passport_label_create(screen, samples[i], 174, 66, 36,
            &lv_font_montserrat_14, lv_color_black(), LV_TEXT_ALIGN_RIGHT);
        lv_obj_update_layout(label);
        assert(lv_obj_get_height(label) == lv_font_get_line_height(passport_font(&lv_font_montserrat_14)));
        assert(lv_obj_get_width(label) == 36);
        assert(lv_label_get_long_mode(label) == LV_LABEL_LONG_MODE_DOTS);
        lv_obj_delete(label);
    }
    check_surface(display, screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    const char *mixed[] = {"立即刷新", "VibeCafe / 已关联", "时区 / Asia/Shanghai",
                          "補齊七日資料", "タイムゾーン", "Language / English"};
    for (unsigned i = 0; i < 6; ++i)
        passport_label_create(screen, mixed[i], 16, 20 + i * 30, 208,
                              &lv_font_montserrat_12, lv_color_black(), LV_TEXT_ALIGN_LEFT);
    lv_obj_update_layout(screen); lv_refr_now(display); save_pixels("passport-font");
    lv_obj_clean(screen);
    for (int kind = 0; kind < 2; ++kind) {
        lv_obj_t *qr = lv_qrcode_create(screen);
        lv_qrcode_set_size(qr, 190);
        lv_qrcode_set_dark_color(qr, lv_color_black());
        lv_qrcode_set_light_color(qr, lv_color_white());
        lv_qrcode_set_quiet_zone(qr, true);
        lv_obj_set_pos(qr, 25, 52);
        const char *url = kind ? VIBE_AUTHOR_URL : VIBE_REPOSITORY_URL;
        assert(lv_qrcode_update(qr, url, strlen(url)) == LV_RESULT_OK);
        lv_obj_update_layout(screen); lv_refr_now(display);
        save_pixels(kind ? "passport-author-qr" : "passport-repository-qr");
        lv_obj_clean(screen);
    }
    lv_display_delete(display);
    lv_deinit();
    puts("Passport real LVGL labels, branding, black corners and arrow pixels: PASS");
}
