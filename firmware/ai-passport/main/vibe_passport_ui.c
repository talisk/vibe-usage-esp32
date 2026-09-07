#include "vibe_passport_ui.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_controller.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "vibe_usage.h"
#include "passport_label.h"
#include "passport_todo_title.h"
#include "passport_surface.h"
#include "vibe_product.h"
#include "vibe_about.h"
#include "vibe_list_navigation.h"

typedef enum {
    PAGE_OVERVIEW = 0,
    PAGE_AGENTS,
    PAGE_STATUS,
    PAGE_TODO,
    PAGE_SETTINGS,
    PAGE_ABOUT,
    PAGE_LLM_CONFIG,
    PAGE_CONFIRM_UNLINK,
    PAGE_CONFIRM_RESET,
} passport_page_t;

typedef enum {
    VISUAL_OVERVIEW = 0,
    VISUAL_AGENTS,
    VISUAL_STATUS,
    VISUAL_TODO,
    VISUAL_LLM_CONFIG,
    VISUAL_SETTINGS,
    VISUAL_ABOUT,
    VISUAL_CONFIRM_UNLINK,
    VISUAL_CONFIRM_RESET,
    VISUAL_WIFI,
    VISUAL_LINK,
    VISUAL_CONNECTING,
    VISUAL_TIME,
    VISUAL_ERROR,
} passport_visual_t;

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} passport_button_event_t;

typedef struct {
    QueueHandle_t events;
    TaskHandle_t task;
    lv_obj_t *screen;
    passport_page_t page;
    vibe_window_t window;
    uint8_t settings_index;
    vibe_about_view_t about_view;
    uint8_t agent_offset;
    uint8_t todo_offset;
    uint8_t wifi_qr_step;
    bool voice_held;
    uint32_t shown_alert_id;
    uint32_t shown_alert_sequence;
    bool confirm_yes;
    bool force_render;
    bool dimmed;
    bool consume_sequence[3];
    uint8_t brightness;
    int battery_soc;
    int battery_mv;
    int64_t last_activity_us;
    int64_t next_battery_sample_us;
    uint32_t rendered_hash;
    /* Keep the large controller snapshot out of the UI task stack. */
    app_controller_view_t view;
} passport_ui_t;

static const char *TAG = "vibe_passport_ui";
static const char *const SETTINGS[] = {
    "Refresh now", "Reconcile 7 days", "Wi-Fi", VIBE_ACCOUNT_NAME,
    "Timezone", "Display", "Alert sound", "About", "Reset Settings", "Language", "LLM config",
};
static const uint8_t SETTINGS_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);
static passport_ui_t s_ui;
static vibe_language_t s_language = VIBE_LANG_EN;
#define TR(text) vibe_tr(s_language, (text))

static const lv_color_t COLOR_INK = {.red = 0x16, .green = 0x20, .blue = 0x25};

static lv_color_t color(uint32_t hex) {
    return lv_color_hex(hex);
}

static const char *state_name(vibe_app_state_t state) {
    switch (state) {
        case VIBE_APP_BOOT: return TR("Booting");
        case VIBE_APP_LOAD_CONFIG: return TR("Loading settings");
        case VIBE_APP_WIFI_REQUIRED: return TR("Wi-Fi required");
        case VIBE_APP_WIFI_PROVISIONING: return TR("Wi-Fi setup");
        case VIBE_APP_WIFI_CONNECTING: return TR("Connecting Wi-Fi");
        case VIBE_APP_TIME_SYNC: return TR("Synchronizing time");
        case VIBE_APP_TIME_REQUIRED: return TR("Valid time required");
        case VIBE_APP_DEVICE_LINK: return TR("Link Vibe account");
        case VIBE_APP_SYNCING: return TR("Syncing usage");
        case VIBE_APP_DASHBOARD: return TR("Ready");
        case VIBE_APP_SETTINGS: return TR("Settings");
        case VIBE_APP_STORAGE_ERROR: return TR("Storage error");
        case VIBE_APP_PREPARE_SLEEP: return TR("Stopping");
        case VIBE_APP_RESETTING: return TR("Resetting");
    }
    return TR("Unknown");
}

static const char *data_name(vibe_data_state_t state) {
    switch (state) {
        case VIBE_READY: return TR("Ready");
        case VIBE_EMPTY: return TR("No usage");
        case VIBE_STALE: return TR("Stale");
        case VIBE_AUTH_REQUIRED: return TR("Auth required");
        case VIBE_LINK_REQUIRED: return TR("Link required");
    }
    return TR("Unknown");
}

static const char *reason_name(app_reason_t reason) {
    switch (reason) {
        case APP_REASON_NONE: return TR("None");
        case APP_REASON_WIFI: return "Wi-Fi";
        case APP_REASON_TIME: return TR("Clock");
        case APP_REASON_AUTH: return TR("Authorization");
        case APP_REASON_HTTP: return TR("Network service");
        case APP_REASON_SCHEMA: return TR("Response format");
        case APP_REASON_STORAGE: return TR("Storage");
        case APP_REASON_CANCELLED: return TR("Cancelled");
    }
    return TR("Unknown");
}

static void format_tokens(uint64_t value, char *output, size_t size) {
    const struct {
        uint64_t divisor;
        const char *suffix;
    } units[] = {
        {1000000000000000000ULL, "E"}, {1000000000000000ULL, "P"},
        {1000000000000ULL, "T"}, {1000000000ULL, "B"},
        {1000000ULL, "M"}, {1000ULL, "K"},
    };
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); ++i) {
        if (value >= units[i].divisor) {
            const uint64_t whole = value / units[i].divisor;
            const uint64_t tenth =
                (value % units[i].divisor) * 10 / units[i].divisor;
            if (whole >= 100 || tenth == 0) {
                snprintf(output, size, "%" PRIu64 "%s", whole,
                         units[i].suffix);
            } else {
                snprintf(output, size, "%" PRIu64 ".%" PRIu64 "%s", whole,
                         tenth, units[i].suffix);
            }
            return;
        }
    }
    snprintf(output, size, "%" PRIu64, value);
}

static void format_time(int64_t epoch, vibe_timezone_t timezone, char *output,
                        size_t size) {
    if (epoch <= 0) {
        snprintf(output, size, TR("Never"));
        return;
    }
    time_t raw = (time_t)epoch;
    struct tm value = {0};
    if (timezone == VIBE_TZ_UTC) {
        gmtime_r(&raw, &value);
    } else {
        localtime_r(&raw, &value);
    }
    strftime(output, size, "%m-%d %H:%M", &value);
}

static uint64_t snapshot_tokens(const vibe_usage_snapshot_t *snapshot) {
    return s_ui.window == VIBE_WINDOW_TODAY ? snapshot->today_tokens
                                             : snapshot->seven_day_tokens;
}

static uint64_t agent_tokens(const vibe_agent_usage_t *agent) {
    return s_ui.window == VIBE_WINDOW_TODAY ? agent->today_tokens
                                             : agent->seven_day_tokens;
}

static uint16_t agent_bp(const vibe_agent_usage_t *agent) {
    return s_ui.window == VIBE_WINDOW_TODAY ? agent->today_bp
                                             : agent->seven_day_bp;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y,
                            int width, const lv_font_t *font,
                            lv_color_t text_color, lv_text_align_t align) {
    return passport_label_create(parent, text, x, y, width, font, text_color, align);
}

static lv_obj_t *make_text_box(lv_obj_t *parent, const char *text, int x, int y,
                               int width, int height, const lv_font_t *font,
                               lv_color_t text_color, lv_text_align_t align) {
    lv_obj_t *label = make_label(parent, text, x, y, width, font, text_color, align);
    lv_obj_set_height(label, height);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int width,
                           int height, uint32_t fill) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_bg_color(card, color(fill), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void draw_header(const app_controller_view_t *view, const char *title) {
    make_label(s_ui.screen, title, 12, 8, 140, &lv_font_montserrat_14,
               COLOR_INK, LV_TEXT_ALIGN_LEFT);
    char status[40];
    if (s_ui.battery_soc >= 0) {
        snprintf(status, sizeof(status), "%s  %d%%",
                 view->wifi_connected ? "WiFi" : "Off", s_ui.battery_soc);
    } else {
        snprintf(status, sizeof(status), "%s",
                 view->wifi_connected ? "WiFi" : "Off");
    }
    make_label(s_ui.screen, status, 156, 9, 72, &lv_font_montserrat_12,
               COLOR_INK, LV_TEXT_ALIGN_RIGHT);
    lv_obj_t *line = lv_obj_create(s_ui.screen);
    lv_obj_set_pos(line, 12, 31);
    lv_obj_set_size(line, 216, 1);
    lv_obj_set_style_bg_color(line, color(0x162025), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
}

static void draw_footer(const char *left, const char *center,
                        const char *right) {
    if (left[0]) passport_footer_arrow(s_ui.screen, false);
    make_label(s_ui.screen, center, left[0] || right[0] ? 56 : 12, 294,
               left[0] || right[0] ? 128 : 216, &lv_font_montserrat_12,
               color(0x50616A), LV_TEXT_ALIGN_CENTER);
    if (right[0]) passport_footer_arrow(s_ui.screen, true);
}

static void draw_bar(int x, int y, int width, uint16_t basis_points) {
    lv_obj_t *bar = lv_bar_create(s_ui.screen);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, width, 7);
    lv_bar_set_range(bar, 0, 10000);
    lv_bar_set_value(bar, basis_points, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, color(0xD8D2C4), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color(0xE86F3A), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
}

static void render_overview(const app_controller_view_t *view) {
    draw_header(view, TR("VIBE / OVERVIEW"));
    const vibe_usage_snapshot_t *snapshot =
        s_ui.window == VIBE_WINDOW_TODAY ? &view->today : &view->seven_day;
    lv_obj_t *summary = make_card(s_ui.screen, 12, 42, 216, 64, 0xF4CBB7);
    make_label(summary,
               s_ui.window == VIBE_WINDOW_TODAY ? TR("TODAY") : TR("LAST 7 DAYS"),
               10, 7, 90, &lv_font_montserrat_14, color(0x7C3E25),
               LV_TEXT_ALIGN_LEFT);
    char tokens[32];
    format_tokens(snapshot_tokens(snapshot), tokens, sizeof(tokens));
    make_label(summary, tokens, 9, 27, 130, &lv_font_montserrat_28,
               COLOR_INK, LV_TEXT_ALIGN_LEFT);
    make_label(summary, TR("TOKENS"), 142, 36, 64, &lv_font_montserrat_14,
               color(0x7C3E25), LV_TEXT_ALIGN_RIGHT);

    const uint8_t count = snapshot->agent_count < 4 ? snapshot->agent_count : 4;
    int y = 116;
    for (uint8_t i = 0; i < count; ++i) {
        const vibe_agent_usage_t *agent = &snapshot->agents[i];
        char value[24];
        format_tokens(agent_tokens(agent), value, sizeof(value));
        make_label(s_ui.screen, agent->id, 16, y, 130,
                   &lv_font_montserrat_14, COLOR_INK, LV_TEXT_ALIGN_LEFT);
        make_label(s_ui.screen, value, 155, y, 68,
                   &lv_font_montserrat_14, COLOR_INK, LV_TEXT_ALIGN_RIGHT);
        draw_bar(16, y + 20, 207, agent_bp(agent));
        y += 38;
    }
    if (count == 0) {
        make_label(s_ui.screen,
                   snapshot->state == VIBE_EMPTY ? TR("No usage in this window")
                                                  : TR("Waiting for first sync"),
                   16, 164, 208, &lv_font_montserrat_14, color(0x50616A),
                   LV_TEXT_ALIGN_CENTER);
    }
    char detail[64];
    snprintf(detail, sizeof(detail), TR("7D coverage %u/7 / %s"),
             view->seven_day_coverage, data_name(snapshot->state));
    make_label(s_ui.screen, detail, 16, 270, 208, &lv_font_montserrat_12,
               color(0x50616A), LV_TEXT_ALIGN_CENTER);
    draw_footer("UP", TR("OK Today / 7D"), "DOWN");
}

static void render_agents(const app_controller_view_t *view) {
    draw_header(view, TR("VIBE / AGENTS"));
    const vibe_usage_snapshot_t *snapshot =
        s_ui.window == VIBE_WINDOW_TODAY ? &view->today : &view->seven_day;
    make_label(s_ui.screen,
               s_ui.window == VIBE_WINDOW_TODAY ? TR("TODAY") : TR("LAST 7 DAYS"),
               14, 40, 130, &lv_font_montserrat_14, color(0x7C3E25),
               LV_TEXT_ALIGN_LEFT);
    const uint8_t visible = 6;
    s_ui.agent_offset = vibe_list_clamp(s_ui.agent_offset, snapshot->agent_count, visible);
    int y = 66;
    for (uint8_t row = 0; row < visible; ++row) {
        const uint8_t index = s_ui.agent_offset + row;
        if (index >= snapshot->agent_count) break;
        const vibe_agent_usage_t *agent = &snapshot->agents[index];
        char rank[8];
        char value[24];
        char percent[16];
        snprintf(rank, sizeof(rank), "%u", index + 1);
        format_tokens(agent_tokens(agent), value, sizeof(value));
        const uint16_t bp = agent_bp(agent);
        snprintf(percent, sizeof(percent), "%u.%u%%", bp / 100,
                 (bp % 100) / 10);
        lv_obj_t *row_card = make_card(s_ui.screen, 12, y - 5, 216, 31,
                                       row % 2 == 0 ? 0xEFEADF : 0xF8F4EA);
        make_label(row_card, rank, 6, 7, 16, &lv_font_montserrat_12,
                   color(0x7C3E25), LV_TEXT_ALIGN_LEFT);
        make_label(row_card, agent->id, 26, 7, 82, &lv_font_montserrat_12,
                   COLOR_INK, LV_TEXT_ALIGN_LEFT);
        make_label(row_card, percent, 112, 7, 48, &lv_font_montserrat_12,
                   color(0x50616A), LV_TEXT_ALIGN_RIGHT);
        make_label(row_card, value, 162, 7, 48, &lv_font_montserrat_12,
                   COLOR_INK, LV_TEXT_ALIGN_RIGHT);
        y += 36;
    }
    if (snapshot->agent_count == 0) {
        make_label(s_ui.screen, TR("No agent usage available"), 16, 146, 208,
                   &lv_font_montserrat_14, color(0x50616A),
                   LV_TEXT_ALIGN_CENTER);
    }
    if (snapshot->sources_collapsed) {
        make_label(s_ui.screen, TR("Small sources grouped as Other"),
                   16, 278, 208, &lv_font_montserrat_12, color(0x50616A),
                   LV_TEXT_ALIGN_CENTER);
    }
    draw_footer("UP", TR("OK Today / 7D"), "DOWN");
}

static void render_status(const app_controller_view_t *view) {
    draw_header(view, TR("VIBE / STATUS"));
    const char *labels[] = {
        TR("System"), "Wi-Fi", TR("Account"), TR("Timezone"), TR("Last sync"),
        TR("Metric"), TR("Today"), TR("7 days"), TR("Firmware"), TR("Battery"),
    };
    char values[10][64] = {{0}};
    snprintf(values[0], sizeof(values[0]), "%s%s", state_name(view->lifecycle),
             view->busy ? TR(" / busy") : "");
    snprintf(values[1], sizeof(values[1]), "%s / %s",
             view->wifi_connected ? TR("Online") : TR("Offline"),
             view->wifi_has_credentials ? TR("saved") : TR("not saved"));
    snprintf(values[2], sizeof(values[2]), "%s",
             view->has_auth ? TR("Linked") : TR("Not linked"));
    snprintf(values[3], sizeof(values[3]), "%s",
             vibe_timezone_name(view->timezone));
    format_time(view->today.last_fetch_at, view->timezone, values[4],
                sizeof(values[4]));
    snprintf(values[5], sizeof(values[5]), "API total v1");
    snprintf(values[6], sizeof(values[6]), "%s", data_name(view->data_state));
    snprintf(values[7], sizeof(values[7]), "%s / %u/7",
             data_name(view->seven_day.state), view->seven_day_coverage);
    snprintf(values[8], sizeof(values[8]), "%s / %s",
             esp_app_get_description()->version, view->device_id);
    if (s_ui.battery_soc >= 0 && s_ui.battery_mv >= 0) {
        snprintf(values[9], sizeof(values[9]), "%d%% / %dmV",
                 s_ui.battery_soc, s_ui.battery_mv);
    } else {
        snprintf(values[9], sizeof(values[9]), TR("Unavailable"));
    }
    for (uint8_t i = 0; i < 10; ++i) {
        const int y = 40 + i * 24;
        make_label(s_ui.screen, labels[i], 14, y, 76,
                   &lv_font_montserrat_12, color(0x7C3E25),
                   LV_TEXT_ALIGN_LEFT);
        make_label(s_ui.screen, values[i], 98, y, 128,
                   &lv_font_montserrat_12, COLOR_INK, LV_TEXT_ALIGN_RIGHT);
    }
    draw_footer("UP", TR("Hold OK menu"), "DOWN");
}

static void render_settings(const app_controller_view_t *view) {
    draw_header(view, TR("SETTINGS"));
    const uint8_t first = s_ui.settings_index >= 9 ? s_ui.settings_index - 8 : 0;
    for (uint8_t i = first; i < SETTINGS_COUNT && i < first + 9; ++i) {
        const int y = 40 + (i - first) * 27;
        const bool selected = i == s_ui.settings_index;
        lv_obj_t *row = make_card(s_ui.screen, 12, y, 216, 24,
                                  selected ? 0x162025 : 0xEFEADF);
        char text[64];
        if (i == 3) {
            snprintf(text, sizeof(text), "%s / %s", TR(SETTINGS[i]),
                     view->has_auth ? TR("linked") : TR("not linked"));
        } else if (i == 4) {
            snprintf(text, sizeof(text), "%s / %s", TR(SETTINGS[i]),
                     vibe_timezone_name(view->timezone));
        } else if (i == 5) {
            snprintf(text, sizeof(text), "%s / %u%%", TR(SETTINGS[i]),
                     s_ui.brightness);
        } else if (i == 6) {
            if (view->alert_volume == 0)
                snprintf(text, sizeof(text), "%s / %s", TR(SETTINGS[i]), TR("Muted"));
            else
                snprintf(text, sizeof(text), "%s / %u%%", TR(SETTINGS[i]), view->alert_volume);
        } else if (i == 9) {
            snprintf(text, sizeof(text), "%s / %s", TR(SETTINGS[i]), vibe_language_name(view->language));
        } else if (i == 10) {
            snprintf(text, sizeof(text), "%s / %s", TR(SETTINGS[i]),
                     view->llm_enabled ? TR("On") : TR("Off"));
        } else {
            snprintf(text, sizeof(text), "%s", TR(SETTINGS[i]));
        }
        make_label(row, text, 8, 5, 200, &lv_font_montserrat_12,
                   selected ? color(0xFFF9EE) : COLOR_INK,
                   LV_TEXT_ALIGN_LEFT);
    }
    draw_footer("UP", TR(strcmp(view->detail, "Could not save language") == 0 ||
                            strcmp(view->detail, "Could not save alert volume") == 0
                            ? "Save failed" : "OK / hold back"), "DOWN");
}

static void render_qr(const char *data, int x, int y, int size);

static void render_about(void) {
    if (s_ui.about_view != VIBE_ABOUT_DETAILS) {
        make_label(s_ui.screen, TR(vibe_about_title(s_ui.about_view)), 16, 12, 208,
                   &lv_font_montserrat_20, COLOR_INK, LV_TEXT_ALIGN_CENTER);
        render_qr(vibe_about_url(s_ui.about_view), 25, 52, 190);
        make_label(s_ui.screen, TR("Scan to visit."), 16, 260, 208,
                   &lv_font_montserrat_14, COLOR_INK, LV_TEXT_ALIGN_CENTER);
        draw_footer("UP", TR("OK back"), "DOWN");
        return;
    }
    make_label(s_ui.screen, VIBE_PASSPORT_ABOUT, 12, 36, 216,
               &lv_font_montserrat_20, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    char version[48];
    snprintf(version, sizeof(version), TR("Version %s"),
             esp_app_get_description()->version);
    make_label(s_ui.screen, version, 12, 76, 216, &lv_font_montserrat_14,
               COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_label(s_ui.screen, TR("Repository"), 16, 122, 208, &lv_font_montserrat_14,
               color(0x7C3E25), LV_TEXT_ALIGN_CENTER);
    make_label(s_ui.screen, VIBE_REPOSITORY_OWNER_URL, 16, 150, 208,
               &lv_font_montserrat_12, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_label(s_ui.screen, VIBE_REPOSITORY_NAME, 16, 168, 208,
               &lv_font_montserrat_12, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_label(s_ui.screen, TR("Author:"), 16, 214, 208, &lv_font_montserrat_14,
               color(0x7C3E25), LV_TEXT_ALIGN_CENTER);
    make_label(s_ui.screen, VIBE_AUTHOR_URL, 16, 242, 208,
               &lv_font_montserrat_12, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_label(s_ui.screen, TR("UP Repo / DOWN X"), 16, 271, 208,
               &lv_font_montserrat_12, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    draw_footer("UP", TR("OK back"), "DOWN");
}

static void render_confirmation(const char *title, const char *detail) {
    make_label(s_ui.screen, title, 16, 45, 208, &lv_font_montserrat_20,
               COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_text_box(s_ui.screen, detail, 22, 94, 196, 48, &lv_font_montserrat_14,
               color(0x50616A), LV_TEXT_ALIGN_CENTER);
    make_text_box(s_ui.screen, TR("This action cannot be undone on this device."),
               22, 146, 196, 48, &lv_font_montserrat_14, color(0x7C3E25),
               LV_TEXT_ALIGN_CENTER);
    lv_obj_t *no = make_card(s_ui.screen, 28, 202, 82, 48,
                             s_ui.confirm_yes ? 0xEFEADF : 0x162025);
    lv_obj_t *yes = make_card(s_ui.screen, 130, 202, 82, 48,
                              s_ui.confirm_yes ? 0xE86F3A : 0xEFEADF);
    make_label(no, TR("NO"), 0, 14, 82, &lv_font_montserrat_14,
               s_ui.confirm_yes ? COLOR_INK : color(0xFFFFFF),
               LV_TEXT_ALIGN_CENTER);
    make_label(yes, TR("YES"), 0, 14, 82, &lv_font_montserrat_14,
               s_ui.confirm_yes ? color(0xFFFFFF) : COLOR_INK,
               LV_TEXT_ALIGN_CENTER);
    draw_footer("UP", TR("OK confirm"), "DOWN");
}

static void render_qr(const char *data, int x, int y, int size) {
    if (data == NULL || data[0] == '\0') {
        make_label(s_ui.screen, TR("QR unavailable"), x, y + size / 2 - 8, size,
                   &lv_font_montserrat_14, color(0xB13724),
                   LV_TEXT_ALIGN_CENTER);
        return;
    }
    lv_obj_t *qr = lv_qrcode_create(s_ui.screen);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, color(0x101619));
    lv_qrcode_set_light_color(qr, color(0xFFFFFF));
    lv_qrcode_set_quiet_zone(qr, true);
    lv_obj_set_pos(qr, x, y);
    if (lv_qrcode_update(qr, data, strlen(data)) != LV_RESULT_OK) {
        lv_obj_delete(qr);
        make_label(s_ui.screen, TR("QR does not fit"), x, y + size / 2 - 8, size,
                   &lv_font_montserrat_14, color(0xB13724),
                   LV_TEXT_ALIGN_CENTER);
    }
}

static void render_wifi(const app_controller_view_t *view) {
    make_label(s_ui.screen, TR(s_ui.wifi_qr_step ? "2. Open setup" : "1. Join Wi-Fi"),
               12, 8, 216, &lv_font_montserrat_20, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    char payload[80];
    snprintf(payload, sizeof(payload), "WIFI:T:nopass;S:%s;;", view->ap_ssid);
    const char *url = view->portal_url[0] ? view->portal_url : "http://192.168.4.1";
    render_qr(s_ui.wifi_qr_step ? url : payload, 30, 42, 180);
    make_label(s_ui.screen, s_ui.wifi_qr_step ? url :
               (view->ap_ssid[0] ? view->ap_ssid : "VibePassport-XXXX"),
               12, 227, 216, &lv_font_montserrat_12, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_text_box(s_ui.screen, TR("NFC: set up tag once with phone"),
               12, 249, 216, 32, &lv_font_montserrat_12, color(0x50616A),
               LV_TEXT_ALIGN_CENTER);
    draw_footer("UP", TR("OK next QR"), "DOWN");
}

static void render_llm_config(const app_controller_view_t *view) {
    if (!view->wifi_connected || !view->llm_portal_url[0]) {
        if (view->wifi_provisioning) {
            render_wifi(view);
            return;
        }
        draw_header(view, TR("LLM config"));
        make_text_box(s_ui.screen, TR(view->llm_config_open ?
                       (view->wifi_connected ? "Starting setup..." : "Connecting Wi-Fi") : "LLM config"),
                       16, 100, 208, 48,
                       &lv_font_montserrat_20, COLOR_INK, LV_TEXT_ALIGN_CENTER);
        make_text_box(s_ui.screen, TR(view->todo_status), 16, 174, 208, 64,
                       &lv_font_montserrat_12, COLOR_INK, LV_TEXT_ALIGN_CENTER);
        draw_footer("", TR("Hold OK back"), "");
        return;
    }
    make_label(s_ui.screen, TR("LLM config"), 12, 8, 216,
               &lv_font_montserrat_20, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    render_qr(view->llm_portal_url, 25, 40, 190);
    make_text_box(s_ui.screen, TR("Same Wi-Fi: scan to configure"), 12, 236, 216, 32,
                   &lv_font_montserrat_12, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_label(s_ui.screen, TR("Private setup / 10 min"), 12, 274, 216,
               &lv_font_montserrat_12, color(0x50616A), LV_TEXT_ALIGN_CENTER);
    draw_footer("", TR("OK close"), "");
}

static void render_todo(const app_controller_view_t *view) {
    draw_header(view, TR("TODO List"));
    const char *status = view->todo_recording ? "Listening / release OK" :
                         view->todo_busy && view->todo_status[0] ? view->todo_status :
                         view->todo_status[0] ? view->todo_status : "Hold OK to speak";
    make_label(s_ui.screen, TR(status), 14, 39, 212, &lv_font_montserrat_12,
               color(0x7C3E25), LV_TEXT_ALIGN_LEFT);
    s_ui.todo_offset = vibe_list_clamp(s_ui.todo_offset, view->todo_count, 3);
    for (uint8_t row = 0; row < 3 && s_ui.todo_offset + row < view->todo_count; ++row) {
        const app_todo_item_view_t *item = &view->todos[s_ui.todo_offset + row];
        const int y = 64 + row * 65;
        lv_obj_t *card = make_card(s_ui.screen, 12, y, 216, 60,
                                   item->id == view->todo_alert_id ? 0xF4CBB7 : 0xEFEADF);
        char marker[24];
        snprintf(marker, sizeof(marker), "%s %lu", item->completed ? "[x]" : "[ ]",
                 (unsigned long)item->id);
        make_label(card, marker, 6, 4, 45, &lv_font_montserrat_12,
                   color(0x7C3E25), LV_TEXT_ALIGN_LEFT);
        passport_todo_title_create(card, item->title, COLOR_INK);
        char due[24], reminder[80];
        if (item->due_utc > 0) {
            format_time(item->due_utc, view->timezone, due, sizeof(due));
            if (item->repeat_seconds)
                snprintf(reminder, sizeof(reminder), "%s / %s", due, TR("Repeating"));
            else snprintf(reminder, sizeof(reminder), "%s", due);
        } else snprintf(reminder, sizeof(reminder), "%s", TR(item->completed ? "Completed" : "No reminder"));
        make_label(card, reminder, 6, 39, 204, &lv_font_montserrat_12,
                   color(0x50616A), LV_TEXT_ALIGN_LEFT);
    }
    if (!view->todo_count)
        make_text_box(s_ui.screen, TR("No tasks yet. Hold OK, then speak."), 20, 115,
                       200, 72, &lv_font_montserrat_14, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    char count[40];
    snprintf(count, sizeof(count), "%u-%u / %u", view->todo_count ? s_ui.todo_offset + 1 : 0,
             (unsigned)(s_ui.todo_offset + 3 < view->todo_count ? s_ui.todo_offset + 3 : view->todo_count),
             view->todo_count);
    make_label(s_ui.screen, count, 12, 265, 216, &lv_font_montserrat_12,
               color(0x50616A), LV_TEXT_ALIGN_CENTER);
    draw_footer("UP", TR(view->todo_alert_id ? "OK dismiss" : "OK back"), "DOWN");
}

static void render_link(const app_controller_view_t *view) {
    make_label(s_ui.screen, TR("LINK VIBE ACCOUNT"), 12, 6, 216,
               &lv_font_montserrat_14, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    render_qr(view->verification_uri, 25, 36, 190);
    make_label(s_ui.screen, view->user_code, 12, 235, 216,
               &lv_font_montserrat_28, color(0xE86F3A),
               LV_TEXT_ALIGN_CENTER);
    int64_t remaining =
        view->link_deadline_monotonic - view->link_now_monotonic;
    if (remaining < 0) remaining = 0;
    char expiry[64];
    snprintf(expiry, sizeof(expiry), TR("Enter code / expires in %u min"),
             (unsigned)((remaining + 59) / 60));
    make_label(s_ui.screen, expiry, 12, 271, 216, &lv_font_montserrat_12,
               color(0x50616A), LV_TEXT_ALIGN_CENTER);
    draw_footer("", TR("OK new code"), "");
}

static void render_connecting(const app_controller_view_t *view) {
    lv_obj_t *mark = make_card(s_ui.screen, 86, 52, 68, 68, 0xE86F3A);
    make_label(mark, "V", 0, 18, 68, &lv_font_montserrat_28,
               color(0xFFFFFF), LV_TEXT_ALIGN_CENTER);
    make_label(s_ui.screen, VIBE_PASSPORT_ABOUT, 12, 142, 216,
               &lv_font_montserrat_20, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_text_box(s_ui.screen, state_name(view->lifecycle), 18, 182, 204, 48,
               &lv_font_montserrat_20, color(0x7C3E25),
               LV_TEXT_ALIGN_CENTER);
    make_text_box(s_ui.screen,
               view->detail[0] ? TR(view->detail) : TR("Preparing dashboard..."),
               22, 234, 196, 48, &lv_font_montserrat_14, color(0x50616A),
               LV_TEXT_ALIGN_CENTER);
    draw_footer("", TR("Hold OK settings"), "");
}

static void render_time(const app_controller_view_t *view) {
    make_label(s_ui.screen, TR("SETTING THE CLOCK"), 12, 48, 216,
               &lv_font_montserrat_14, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_text_box(s_ui.screen,
               view->lifecycle == VIBE_APP_TIME_SYNC
                   ? TR("Synchronizing securely over NTP")
                   : TR("A valid clock is required"),
               20, 106, 200, 32, &lv_font_montserrat_14, color(0x7C3E25),
               LV_TEXT_ALIGN_CENTER);
    make_text_box(s_ui.screen,
               TR("Usage is never assigned to a guessed date."), 22, 152, 196, 48,
               &lv_font_montserrat_14, COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_text_box(s_ui.screen, TR(view->detail), 22, 206, 196, 64,
               &lv_font_montserrat_14, color(0x50616A),
               LV_TEXT_ALIGN_CENTER);
    draw_footer("", TR("OK retry"), "");
}

static void render_error(const app_controller_view_t *view) {
    make_label(s_ui.screen, TR("ACTION NEEDED"), 12, 38, 216,
               &lv_font_montserrat_20, color(0xB13724),
               LV_TEXT_ALIGN_CENTER);
    char reason[64];
    snprintf(reason, sizeof(reason), TR("%s problem"), reason_name(view->reason));
    make_label(s_ui.screen, reason, 18, 96, 204, &lv_font_montserrat_20,
               COLOR_INK, LV_TEXT_ALIGN_CENTER);
    make_text_box(s_ui.screen,
               view->detail[0] ? TR(view->detail) : state_name(view->lifecycle),
               22, 130, 196, 48, &lv_font_montserrat_14, color(0x50616A),
               LV_TEXT_ALIGN_CENTER);
    char diagnostic[96];
    snprintf(diagnostic, sizeof(diagnostic), "HTTP %d / system 0x%x / %s",
             view->last_http_status, (unsigned)view->last_system_error,
             vibe_error_name(view->last_error));
    make_text_box(s_ui.screen, diagnostic, 18, 182, 204, 48,
               &lv_font_montserrat_14, color(0x7C3E25),
               LV_TEXT_ALIGN_CENTER);
    make_text_box(s_ui.screen,
               view->has_today ? TR("Last known good data is preserved.")
                               : TR("No trusted usage data is available yet."),
               20, 236, 200, 48, &lv_font_montserrat_14, COLOR_INK,
               LV_TEXT_ALIGN_CENTER);
    draw_footer("", TR("OK retry / hold menu"), "");
}

static passport_visual_t resolve_visual(const app_controller_view_t *view) {
    switch (s_ui.page) {
        case PAGE_TODO: return VISUAL_TODO;
        case PAGE_LLM_CONFIG: return VISUAL_LLM_CONFIG;
        case PAGE_SETTINGS: return VISUAL_SETTINGS;
        case PAGE_ABOUT: return VISUAL_ABOUT;
        case PAGE_CONFIRM_UNLINK: return VISUAL_CONFIRM_UNLINK;
        case PAGE_CONFIRM_RESET: return VISUAL_CONFIRM_RESET;
        default: break;
    }
    switch (view->lifecycle) {
        case VIBE_APP_WIFI_REQUIRED:
        case VIBE_APP_WIFI_PROVISIONING:
            return VISUAL_WIFI;
        case VIBE_APP_WIFI_CONNECTING:
            return VISUAL_CONNECTING;
        case VIBE_APP_TIME_SYNC:
        case VIBE_APP_TIME_REQUIRED:
            return VISUAL_TIME;
        case VIBE_APP_DEVICE_LINK:
            if (view->user_code[0]) return VISUAL_LINK;
            return view->reason == APP_REASON_NONE ? VISUAL_CONNECTING
                                                    : VISUAL_ERROR;
        case VIBE_APP_STORAGE_ERROR:
            return VISUAL_ERROR;
        case VIBE_APP_BOOT:
        case VIBE_APP_LOAD_CONFIG:
        case VIBE_APP_RESETTING:
            return VISUAL_CONNECTING;
        default:
            break;
    }
    if (view->reason != APP_REASON_NONE && !view->has_today &&
        view->lifecycle != VIBE_APP_SYNCING) {
        return VISUAL_ERROR;
    }
    switch (s_ui.page) {
        case PAGE_AGENTS: return VISUAL_AGENTS;
        case PAGE_STATUS: return VISUAL_STATUS;
        default: return VISUAL_OVERVIEW;
    }
}

static bool render(const app_controller_view_t *view) {
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGW(TAG, "LVGL lock timed out");
        return false;
    }
    lv_obj_clean(s_ui.screen);
    s_language = view->language;
    lv_obj_set_style_bg_color(s_ui.screen, color(0xFFF9EE), 0);
    lv_obj_set_style_bg_opa(s_ui.screen, LV_OPA_COVER, 0);
    passport_visual_t visual = resolve_visual(view);
    switch (visual) {
        case VISUAL_OVERVIEW: render_overview(view); break;
        case VISUAL_AGENTS: render_agents(view); break;
        case VISUAL_STATUS: render_status(view); break;
        case VISUAL_TODO: render_todo(view); break;
        case VISUAL_LLM_CONFIG: render_llm_config(view); break;
        case VISUAL_SETTINGS: render_settings(view); break;
        case VISUAL_ABOUT: render_about(); break;
        case VISUAL_CONFIRM_UNLINK:
            render_confirmation(TR("UNLINK ACCOUNT?"),
                                TR("Removes API credentials and cached usage."));
            break;
        case VISUAL_CONFIRM_RESET:
            render_confirmation(TR("RESET SETTINGS?"),
                                TR("Removes Wi-Fi, account, cache and settings."));
            break;
        case VISUAL_WIFI: render_wifi(view); break;
        case VISUAL_LINK: render_link(view); break;
        case VISUAL_CONNECTING: render_connecting(view); break;
        case VISUAL_TIME: render_time(view); break;
        case VISUAL_ERROR: render_error(view); break;
    }
    bsp_lvgl_unlock();
    return true;
}

static uint32_t hash_bytes(uint32_t hash, const void *data, size_t size) {
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t render_hash(const app_controller_view_t *view) {
#define HASH_FIELD(field) \
    hash = hash_bytes(hash, &view->field, sizeof(view->field))
    uint32_t hash = 2166136261U;
    HASH_FIELD(lifecycle);
    HASH_FIELD(language);
    HASH_FIELD(alert_volume);
    HASH_FIELD(data_state);
    HASH_FIELD(reason);
    HASH_FIELD(busy);
    HASH_FIELD(wifi_has_credentials);
    HASH_FIELD(wifi_connected);
    HASH_FIELD(wifi_provisioning);
    HASH_FIELD(time_valid);
    HASH_FIELD(has_auth);
    HASH_FIELD(has_today);
    HASH_FIELD(has_seven_day);
    HASH_FIELD(seven_day_coverage);
    HASH_FIELD(generation);
    HASH_FIELD(config_revision);
    HASH_FIELD(refresh_seconds);
    HASH_FIELD(timezone);
    HASH_FIELD(link_deadline_monotonic);
    int64_t link_minutes =
        view->link_deadline_monotonic - view->link_now_monotonic;
    if (link_minutes < 0) link_minutes = 0;
    link_minutes = (link_minutes + 59) / 60;
    hash = hash_bytes(hash, &link_minutes, sizeof(link_minutes));
    HASH_FIELD(last_error);
    HASH_FIELD(last_system_error);
    HASH_FIELD(last_http_status);
    HASH_FIELD(device_id);
    HASH_FIELD(ap_ssid);
    HASH_FIELD(portal_url);
    HASH_FIELD(user_code);
    HASH_FIELD(verification_uri);
    HASH_FIELD(detail);
    HASH_FIELD(llm_config_open);
    HASH_FIELD(llm_enabled);
    HASH_FIELD(llm_portal_url);
    HASH_FIELD(todo_status);
    HASH_FIELD(todo_recording);
    HASH_FIELD(todo_busy);
    HASH_FIELD(todo_revision);
    HASH_FIELD(todo_count);
    HASH_FIELD(todo_alert_id);
    HASH_FIELD(todo_alert_sequence);
    HASH_FIELD(today);
    HASH_FIELD(seven_day);
#undef HASH_FIELD
    hash = hash_bytes(hash, &s_ui.page, sizeof(s_ui.page));
    hash = hash_bytes(hash, &s_ui.window, sizeof(s_ui.window));
    hash = hash_bytes(hash, &s_ui.settings_index, sizeof(s_ui.settings_index));
    hash = hash_bytes(hash, &s_ui.about_view, sizeof(s_ui.about_view));
    hash = hash_bytes(hash, &s_ui.agent_offset, sizeof(s_ui.agent_offset));
    hash = hash_bytes(hash, &s_ui.todo_offset, sizeof(s_ui.todo_offset));
    hash = hash_bytes(hash, &s_ui.wifi_qr_step, sizeof(s_ui.wifi_qr_step));
    hash = hash_bytes(hash, &s_ui.confirm_yes, sizeof(s_ui.confirm_yes));
    hash = hash_bytes(hash, &s_ui.brightness, sizeof(s_ui.brightness));
    hash = hash_bytes(hash, &s_ui.battery_soc, sizeof(s_ui.battery_soc));
    hash = hash_bytes(hash, &s_ui.battery_mv, sizeof(s_ui.battery_mv));
    return hash;
}

static void controller_changed(void *context) {
    passport_ui_t *ui = context;
    if (ui != NULL && ui->task != NULL) {
        xTaskNotifyGive(ui->task);
    }
}

static void button_changed(bsp_btn_t button, bsp_btn_ev_t event,
                           void *context) {
    passport_ui_t *ui = context;
    if (ui == NULL || ui->events == NULL) return;
    /* A release must stop capture even while LVGL is busy drawing. */
    if (button == BSP_BTN_OK && event == BSP_BTN_RELEASE) app_controller_voice_stop();
    const passport_button_event_t queued = {
        .button = button,
        .event = event,
    };
    xQueueSend(ui->events, &queued, 0);
    if (ui->task != NULL) xTaskNotifyGive(ui->task);
}

static void dispatch_settings(const app_controller_view_t *view) {
    switch (s_ui.settings_index) {
        case 0:
            app_controller_dispatch(APP_INTENT_REFRESH);
            s_ui.page = PAGE_OVERVIEW;
            break;
        case 1:
            app_controller_dispatch(APP_INTENT_RECONCILE);
            s_ui.page = PAGE_OVERVIEW;
            break;
        case 2:
            s_ui.wifi_qr_step = 0;
            app_controller_dispatch(APP_INTENT_RECONFIGURE_WIFI);
            s_ui.page = PAGE_OVERVIEW;
            break;
        case 3:
            if (view->has_auth) {
                s_ui.confirm_yes = false;
                s_ui.page = PAGE_CONFIRM_UNLINK;
            } else {
                app_controller_dispatch(APP_INTENT_RELINK);
                s_ui.page = PAGE_OVERVIEW;
            }
            break;
        case 4:
            app_controller_dispatch(APP_INTENT_TOGGLE_TIMEZONE);
            s_ui.page = PAGE_STATUS;
            break;
        case 5:
            s_ui.brightness = s_ui.brightness == 100 ? 60 : 100;
            bsp_display_backlight(s_ui.brightness);
            s_ui.dimmed = false;
            break;
        case 6:
            app_controller_dispatch(APP_INTENT_CYCLE_ALERT_VOLUME);
            break;
        case 7:
            s_ui.about_view = VIBE_ABOUT_DETAILS;
            s_ui.page = PAGE_ABOUT;
            break;
        case 8:
            s_ui.confirm_yes = false;
            s_ui.page = PAGE_CONFIRM_RESET;
            break;
        case 9:
            app_controller_dispatch(APP_INTENT_CYCLE_LANGUAGE);
            break;
        case 10:
            s_ui.page = PAGE_LLM_CONFIG;
            s_ui.wifi_qr_step = 0;
            app_controller_dispatch(APP_INTENT_LLM_CONFIG);
            break;
        default:
            break;
    }
}

static void handle_business_button(const passport_button_event_t *event,
                                   const app_controller_view_t *view) {
    if (event->event == BSP_BTN_RELEASE) {
        if (event->button == BSP_BTN_OK && s_ui.voice_held) {
            s_ui.voice_held = false;
            app_controller_voice_stop();
        }
        return;
    }
    if (event->event == BSP_BTN_LONG) {
        if (event->button == BSP_BTN_OK && s_ui.page == PAGE_TODO) {
            if (!view->todo_busy && !view->todo_recording && bsp_button_is_pressed(BSP_BTN_OK)) {
                s_ui.voice_held = true;
                app_controller_dispatch(APP_INTENT_TODO_VOICE_START);
            }
            return;
        }
        if (event->button == BSP_BTN_OK && s_ui.page == PAGE_LLM_CONFIG) {
            app_controller_dispatch(APP_INTENT_LLM_CONFIG_CLOSE);
            s_ui.page = PAGE_SETTINGS;
            return;
        }
        if (event->button == BSP_BTN_OK) {
            s_ui.page = s_ui.page == PAGE_SETTINGS ? PAGE_OVERVIEW
                                                   : PAGE_SETTINGS;
            s_ui.confirm_yes = false;
            s_ui.force_render = true;
        }
        return;
    }
    if (event->event != BSP_BTN_CLICK || s_ui.voice_held) return;
    if (s_ui.page == PAGE_TODO) {
        if (view->todo_busy || view->todo_recording) return;
        if (event->button == BSP_BTN_OK) {
            if (view->todo_alert_id) app_controller_dispatch(APP_INTENT_TODO_ACK);
            else s_ui.page = PAGE_OVERVIEW;
        } else if (!vibe_list_step(&s_ui.todo_offset, view->todo_count, 3,
                                   event->button == BSP_BTN_DOWN)) {
            s_ui.page = event->button == BSP_BTN_UP ? PAGE_STATUS : PAGE_OVERVIEW;
            s_ui.todo_offset = 0;
        }
        s_ui.force_render = true;
        return;
    }
    if (s_ui.page == PAGE_LLM_CONFIG) {
        if (view->wifi_provisioning && !view->wifi_connected) {
            s_ui.wifi_qr_step = !s_ui.wifi_qr_step;
        } else if (event->button == BSP_BTN_OK) {
            app_controller_dispatch(APP_INTENT_LLM_CONFIG_CLOSE);
            s_ui.page = PAGE_SETTINGS;
        }
        s_ui.force_render = true;
        return;
    }

    if (s_ui.page == PAGE_CONFIRM_UNLINK ||
        s_ui.page == PAGE_CONFIRM_RESET) {
        if (event->button == BSP_BTN_UP || event->button == BSP_BTN_DOWN) {
            s_ui.confirm_yes = !s_ui.confirm_yes;
        } else if (event->button == BSP_BTN_OK) {
            if (s_ui.confirm_yes) {
                app_controller_dispatch(
                    s_ui.page == PAGE_CONFIRM_UNLINK ? APP_INTENT_UNLINK
                                                     : APP_INTENT_FACTORY_RESET);
                s_ui.page = PAGE_OVERVIEW;
            } else {
                s_ui.page = PAGE_SETTINGS;
            }
            s_ui.confirm_yes = false;
        }
        s_ui.force_render = true;
        return;
    }
    if (s_ui.page == PAGE_SETTINGS) {
        if (event->button == BSP_BTN_UP) {
            s_ui.settings_index = s_ui.settings_index == 0
                                      ? SETTINGS_COUNT - 1
                                      : s_ui.settings_index - 1;
        } else if (event->button == BSP_BTN_DOWN) {
            s_ui.settings_index =
                (s_ui.settings_index + 1) % SETTINGS_COUNT;
        } else {
            dispatch_settings(view);
        }
        s_ui.force_render = true;
        return;
    }
    if (s_ui.page == PAGE_ABOUT) {
        if (event->button == BSP_BTN_OK && s_ui.about_view == VIBE_ABOUT_DETAILS)
            s_ui.page = PAGE_SETTINGS;
        else
            s_ui.about_view = vibe_about_navigate(s_ui.about_view,
                event->button == BSP_BTN_UP ? VIBE_ABOUT_UP :
                event->button == BSP_BTN_DOWN ? VIBE_ABOUT_DOWN : VIBE_ABOUT_BACK);
        s_ui.force_render = true;
        return;
    }

    const passport_visual_t visual = resolve_visual(view);
    if (visual == VISUAL_WIFI || visual == VISUAL_LINK ||
        visual == VISUAL_TIME || visual == VISUAL_ERROR) {
        if (event->button == BSP_BTN_OK) {
            if (visual == VISUAL_WIFI) {
                if (view->wifi_provisioning) s_ui.wifi_qr_step = !s_ui.wifi_qr_step;
                else app_controller_dispatch(APP_INTENT_START_WIFI);
                s_ui.force_render = true;
            } else if (visual == VISUAL_LINK) {
                app_controller_dispatch(APP_INTENT_RELINK);
            } else {
                app_controller_dispatch(APP_INTENT_REFRESH);
            }
        } else {
            s_ui.page = PAGE_TODO;
            s_ui.force_render = true;
        }
        return;
    }

    if (event->button == BSP_BTN_OK) {
        if (s_ui.page == PAGE_OVERVIEW || s_ui.page == PAGE_AGENTS) {
            s_ui.agent_offset = 0;
            s_ui.window = s_ui.window == VIBE_WINDOW_TODAY
                              ? VIBE_WINDOW_SEVEN_DAYS
                              : VIBE_WINDOW_TODAY;
        }
    } else if (s_ui.page == PAGE_AGENTS) {
        const vibe_usage_snapshot_t *snapshot =
            s_ui.window == VIBE_WINDOW_TODAY ? &view->today : &view->seven_day;
        if (!vibe_list_step(&s_ui.agent_offset, snapshot->agent_count, 6,
                            event->button == BSP_BTN_DOWN)) {
            s_ui.page =
                event->button == BSP_BTN_UP ? PAGE_OVERVIEW : PAGE_STATUS;
            s_ui.agent_offset = 0;
        }
    } else {
        int page = (int)s_ui.page;
        page += event->button == BSP_BTN_UP ? -1 : 1;
        if (page < PAGE_OVERVIEW) page = PAGE_TODO;
        if (page > PAGE_TODO) page = PAGE_OVERVIEW;
        s_ui.page = (passport_page_t)page;
        s_ui.agent_offset = 0;
    }
    s_ui.force_render = true;
}

static void handle_button(const passport_button_event_t *event,
                          const app_controller_view_t *view) {
    if (event->button < BSP_BTN_UP || event->button > BSP_BTN_OK) return;
    s_ui.last_activity_us = esp_timer_get_time();
    if (s_ui.dimmed) {
        bsp_display_backlight(s_ui.brightness);
        s_ui.dimmed = false;
        s_ui.consume_sequence[event->button] = true;
        return;
    }
    if (event->event == BSP_BTN_PRESS) return;
    if (s_ui.consume_sequence[event->button]) {
        if (event->event == BSP_BTN_CLICK || event->event == BSP_BTN_DOUBLE ||
            event->event == BSP_BTN_LONG) {
            s_ui.consume_sequence[event->button] = false;
        }
        return;
    }
    handle_business_button(event, view);
}

static void ui_task(void *argument) {
    passport_ui_t *ui = argument;
    ui->task = xTaskGetCurrentTaskHandle();
    while (true) {
        app_controller_view_t *view = &ui->view;
        app_controller_get_view(view);
        /* A full UI queue must not strand the local hold latch after release. */
        if (ui->voice_held && !bsp_button_is_pressed(BSP_BTN_OK)) ui->voice_held = false;
        if (view->todo_alert_id && (view->todo_alert_id != ui->shown_alert_id ||
                                    view->todo_alert_sequence != ui->shown_alert_sequence)) {
            ui->shown_alert_id = view->todo_alert_id;
            ui->shown_alert_sequence = view->todo_alert_sequence;
            ui->page = PAGE_TODO;
            ui->todo_offset = 0;
            for (uint8_t i = 0; i < view->todo_count; ++i)
                if (view->todos[i].id == view->todo_alert_id) ui->todo_offset = i;
            bsp_display_backlight(ui->brightness);
            ui->dimmed = false;
            ui->last_activity_us = esp_timer_get_time();
            ui->force_render = true;
        } else if (!view->todo_alert_id) ui->shown_alert_id = 0;
        passport_button_event_t event;
        while (xQueueReceive(ui->events, &event, 0) == pdTRUE) {
            handle_button(&event, view);
            app_controller_get_view(view);
        }

        const int64_t now = esp_timer_get_time();
        if (now >= ui->next_battery_sample_us) {
            ui->battery_soc = bsp_battery_soc();
            ui->battery_mv = bsp_battery_mv();
            ui->next_battery_sample_us = now + 60LL * 1000000LL;
            ui->force_render = true;
        }
        if (!ui->dimmed &&
            now - ui->last_activity_us >= 30LL * 1000000LL) {
            uint8_t dim = ui->brightness / 5;
            if (dim == 0) dim = 1;
            bsp_display_backlight(dim);
            ui->dimmed = true;
        }

        const uint32_t hash = render_hash(view);
        if (ui->force_render || hash != ui->rendered_hash) {
            if (render(view)) {
                ui->rendered_hash = hash;
                ui->force_render = false;
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
}

static bool held_at_boot(bsp_btn_t button, uint32_t duration_ms) {
    if (!bsp_button_is_pressed(button)) return false;
    const int64_t deadline =
        esp_timer_get_time() + (int64_t)duration_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        if (!bsp_button_is_pressed(button)) return false;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    return bsp_button_is_pressed(button);
}

esp_err_t vibe_passport_run(const device_config_t *settings) {
    if (settings == NULL) return ESP_ERR_INVALID_ARG;
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.page = PAGE_OVERVIEW;
    s_ui.window = VIBE_WINDOW_TODAY;
    s_ui.brightness = 100;
    s_ui.battery_soc = -1;
    s_ui.battery_mv = -1;
    s_ui.last_activity_us = esp_timer_get_time();
    s_ui.next_battery_sample_us = s_ui.last_activity_us;
    s_ui.force_render = true;
    s_ui.events = xQueueCreate(16, sizeof(passport_button_event_t));
    if (s_ui.events == NULL) return ESP_ERR_NO_MEM;

    esp_err_t error = bsp_display_init();
    if (error != ESP_OK || bsp_lvgl_init() == NULL) {
        return error == ESP_OK ? ESP_FAIL : error;
    }
    bsp_display_backlight(s_ui.brightness);
    if (bsp_lvgl_lock(1000)) {
        lv_obj_t *root = lv_obj_create(NULL);
        s_ui.screen = passport_surface_create(root);
        lv_screen_load(root);
        bsp_lvgl_unlock();
    }
    if (s_ui.screen == NULL) return ESP_ERR_NO_MEM;

    error = bsp_button_init(button_changed, &s_ui);
    if (error != ESP_OK) return error;
    const bool reconfigure_wifi = held_at_boot(BSP_BTN_OK, 2000);
    xQueueReset(s_ui.events);
    if (reconfigure_wifi) s_ui.consume_sequence[BSP_BTN_OK] = true;

    error = bsp_battery_init();
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "battery gauge unavailable: %s", esp_err_to_name(error));
    }

    char hostname[32];
    snprintf(hostname, sizeof(hostname), "VibePassport-%s",
             settings->device_id);
    app_controller_config_t controller_config = {
        .bootstrap_origin = "https://vibecafe.ai",
        .client_name = VIBE_PASSPORT_NAME,
        .product_prefix = "VibePassport",
        .hostname = hostname,
        .settings = *settings,
        .changed_callback = controller_changed,
        .changed_context = &s_ui,
    };
    error = app_controller_init(&controller_config);
    if (error != ESP_OK) return error;

    if (xTaskCreate(ui_task, "passport_ui", 8192, &s_ui, 5,
                    &s_ui.task) != pdPASS) {
        s_ui.task = NULL;
        return ESP_ERR_NO_MEM;
    }
    error = app_controller_start();
    if (error != ESP_OK) return error;
    if (reconfigure_wifi) {
        ESP_LOGI(TAG, "boot OK hold requested Wi-Fi setup");
        app_controller_dispatch(APP_INTENT_RECONFIGURE_WIFI);
    }
    return ESP_OK;
}
