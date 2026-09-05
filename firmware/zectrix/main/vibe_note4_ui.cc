#include "vibe_note4_ui.h"
#include "note_qr.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "esp_app_desc.h"
#include "epd_refresh_policy.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/task.h"
#include "rtc_pcf8563.h"
#include "vibe_usage.h"
#include "vibe_list_navigation.h"
#include "vibe_product.h"

namespace {

constexpr char kTag[] = "vibe_note4_ui";
constexpr int kFooterY = 278;
static vibe_language_t s_language = VIBE_LANG_EN;
#define TR(text) vibe_tr(s_language, (text))
constexpr uint8_t kSettingsCount = 9;
constexpr const char* kSettings[kSettingsCount] = {
    "Refresh now", "Reconcile 7 days", "Wi-Fi setup", VIBE_ACCOUNT_NAME,
    "Timezone", "Display / full", "About", "Reset Settings", "Language",
};

struct QrDrawContext {
    ZectrixCanvas* canvas = nullptr;
    int x = 0;
    int y = 0;
    int extent = 0;
    bool drawn = false;
};

const char* StateName(vibe_app_state_t state) {
    switch (state) {
        case VIBE_APP_BOOT: return TR("Boot");
        case VIBE_APP_LOAD_CONFIG: return TR("Loading");
        case VIBE_APP_WIFI_REQUIRED: return TR("Wi-Fi required");
        case VIBE_APP_WIFI_PROVISIONING: return TR("Wi-Fi setup");
        case VIBE_APP_WIFI_CONNECTING: return TR("Wi-Fi connecting");
        case VIBE_APP_TIME_SYNC: return TR("Time sync");
        case VIBE_APP_TIME_REQUIRED: return TR("Time required");
        case VIBE_APP_DEVICE_LINK: return TR("Account link");
        case VIBE_APP_SYNCING: return TR("Syncing");
        case VIBE_APP_DASHBOARD: return TR("Ready");
        case VIBE_APP_SETTINGS: return TR("Settings");
        case VIBE_APP_STORAGE_ERROR: return TR("Storage error");
        case VIBE_APP_PREPARE_SLEEP: return TR("Stopping");
        case VIBE_APP_RESETTING: return TR("Resetting");
    }
    return TR("Unknown");
}

const char* DataName(vibe_data_state_t state) {
    switch (state) {
        case VIBE_READY: return TR("ready");
        case VIBE_EMPTY: return TR("empty");
        case VIBE_STALE: return TR("stale");
        case VIBE_AUTH_REQUIRED: return TR("auth required");
        case VIBE_LINK_REQUIRED: return TR("link required");
    }
    return TR("unknown");
}

const char* ReasonName(app_reason_t reason) {
    switch (reason) {
        case APP_REASON_NONE: return TR("none");
        case APP_REASON_WIFI: return "Wi-Fi";
        case APP_REASON_TIME: return TR("time");
        case APP_REASON_AUTH: return TR("authentication");
        case APP_REASON_HTTP: return "HTTP";
        case APP_REASON_SCHEMA: return TR("response schema");
        case APP_REASON_STORAGE: return TR("storage");
        case APP_REASON_CANCELLED: return TR("cancelled");
    }
    return TR("unknown");
}

void FormatTokens(uint64_t value, char* output, size_t size) {
    struct Unit {
        uint64_t divisor;
        const char* suffix;
    };
    constexpr Unit units[] = {
        {1000000000000000000ULL, "E"},
        {1000000000000000ULL, "P"},
        {1000000000000ULL, "T"},
        {1000000000ULL, "B"},
        {1000000ULL, "M"},
        {1000ULL, "K"},
    };
    for (const Unit& unit : units) {
        if (value >= unit.divisor) {
            const uint64_t whole = value / unit.divisor;
            const uint64_t tenth = (value % unit.divisor) * 10 / unit.divisor;
            if (whole >= 100 || tenth == 0) {
                std::snprintf(output, size, "%" PRIu64 "%s", whole,
                              unit.suffix);
            } else {
                std::snprintf(output, size, "%" PRIu64 ".%" PRIu64 "%s",
                              whole, tenth, unit.suffix);
            }
            return;
        }
    }
    std::snprintf(output, size, "%" PRIu64, value);
}

void FormatTime(int64_t epoch, vibe_timezone_t timezone, char* output,
                size_t size) {
    if (epoch <= 0) {
        std::snprintf(output, size, TR("never"));
        return;
    }
    time_t raw = static_cast<time_t>(epoch);
    tm value = {};
    if (timezone == VIBE_TZ_UTC) {
        gmtime_r(&raw, &value);
    } else {
        localtime_r(&raw, &value);
    }
    std::strftime(output, size, "%m-%d %H:%M", &value);
}

void SafeAgentName(const char* input, char* output, size_t size) {
    if (size == 0) {
        return;
    }
    size_t index = 0;
    if (input != nullptr) {
        while (input[index] != '\0' && index + 1 < size) {
            const unsigned char value = static_cast<unsigned char>(input[index]);
            output[index] = value >= 0x20 && value <= 0x7e
                                ? static_cast<char>(value)
                                : '?';
            ++index;
        }
    }
    output[index] = '\0';
}

uint64_t SnapshotTokens(const vibe_usage_snapshot_t& snapshot,
                        vibe_window_t window) {
    return window == VIBE_WINDOW_TODAY ? snapshot.today_tokens
                                        : snapshot.seven_day_tokens;
}

uint64_t AgentTokens(const vibe_agent_usage_t& agent,
                     vibe_window_t window) {
    return window == VIBE_WINDOW_TODAY ? agent.today_tokens
                                        : agent.seven_day_tokens;
}

uint16_t AgentBasisPoints(const vibe_agent_usage_t& agent,
                          vibe_window_t window) {
    return window == VIBE_WINDOW_TODAY ? agent.today_bp
                                        : agent.seven_day_bp;
}

}  // namespace

esp_err_t VibeNote4App::Run(const device_config_t& settings) {
    esp_err_t error = board_.Init();
    if (error != ESP_OK) {
        return error;
    }

    const bool reconfigure_wifi = HeldAtBoot(ZectrixButton::kOk, 2000);
    zectrix_epd_config_t epd_config = {};
    zectrix_epd_get_default_config(&epd_config);
    error = zectrix_epd_new(&epd_config, &epd_);
    if (error != ESP_OK) {
        return error;
    }

    power_ = board_.ReadPowerSnapshot();
    RestoreTimeFromRtc(settings.timezone);

    char hostname[32] = {};
    std::snprintf(hostname, sizeof(hostname), "VibeNote-%s",
                  settings.device_id);
    app_controller_config_t controller_config = {};
    controller_config.bootstrap_origin = "https://vibecafe.ai";
    controller_config.client_name = VIBE_NOTE_NAME;
    controller_config.product_prefix = "VibeNote";
    controller_config.hostname = hostname;
    controller_config.settings = settings;
    controller_config.time_synchronized_callback = TimeSynchronized;
    controller_config.time_context = this;
    error = app_controller_init(&controller_config);
    if (error != ESP_OK) {
        return error;
    }

    app_controller_get_view(&view_);
    Render(view_, true);
    error = app_controller_start();
    if (error != ESP_OK) {
        return error;
    }
    if (reconfigure_wifi) {
        ESP_LOGI(kTag, "boot OK hold requested Wi-Fi setup");
        app_controller_dispatch(APP_INTENT_RECONFIGURE_WIFI);
    }

    Loop();
    return ESP_OK;
}

void VibeNote4App::TimeSynchronized(int64_t utc_seconds, void* context) {
    if (context != nullptr) {
        static_cast<VibeNote4App*>(context)->StoreTimeInRtc(utc_seconds);
    }
}

bool VibeNote4App::RestoreTimeFromRtc(vibe_timezone_t timezone) {
    RtcPcf8563* rtc = board_.rtc();
    if (rtc == nullptr) {
        return false;
    }
    setenv("TZ", vibe_timezone_posix(timezone), 1);
    tzset();
    tm local = {};
    if (!rtc->GetTime(local) || local.tm_year < 120 || local.tm_year > 199 ||
        local.tm_mon < 0 || local.tm_mon > 11 || local.tm_mday < 1 ||
        local.tm_mday > 31 || local.tm_hour < 0 || local.tm_hour > 23 ||
        local.tm_min < 0 || local.tm_min > 59 || local.tm_sec < 0 ||
        local.tm_sec > 59) {
        ESP_LOGW(kTag, "RTC contains no trustworthy wall clock");
        return false;
    }
    const tm expected = local;
    local.tm_isdst = -1;
    const time_t epoch = mktime(&local);
    if (epoch < 1577836800) {
        return false;
    }
    tm round_trip = {};
    localtime_r(&epoch, &round_trip);
    if (round_trip.tm_year != expected.tm_year ||
        round_trip.tm_mon != expected.tm_mon ||
        round_trip.tm_mday != expected.tm_mday ||
        round_trip.tm_hour != expected.tm_hour ||
        round_trip.tm_min != expected.tm_min ||
        round_trip.tm_sec != expected.tm_sec) {
        ESP_LOGW(kTag, "RTC contains a normalized or impossible date");
        return false;
    }
    const timeval value = {.tv_sec = epoch, .tv_usec = 0};
    if (settimeofday(&value, nullptr) != 0) {
        ESP_LOGW(kTag, "failed to restore system clock from RTC");
        return false;
    }
    ESP_LOGI(kTag, "system clock restored from RTC");
    return true;
}

void VibeNote4App::StoreTimeInRtc(int64_t utc_seconds) {
    RtcPcf8563* rtc = board_.rtc();
    if (rtc == nullptr || utc_seconds <= 0) {
        return;
    }
    const time_t epoch = static_cast<time_t>(utc_seconds);
    tm local = {};
    localtime_r(&epoch, &local);
    if (!rtc->SetTime(local)) {
        ESP_LOGW(kTag, "failed to persist synchronized time to RTC");
    }
}

bool VibeNote4App::HeldAtBoot(ZectrixButton button, uint32_t duration_ms) {
    if (!board_.IsButtonPressed(button)) {
        return false;
    }
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms);
    while (static_cast<int32_t>(deadline - xTaskGetTickCount()) > 0) {
        if (!board_.IsButtonPressed(button)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    return board_.IsButtonPressed(button);
}

void VibeNote4App::Loop() {
    while (true) {
        if (app_controller_get_view(&view_) == ESP_OK) {
            const int64_t now = time(nullptr);
            if (next_power_sample_utc_ == 0 || now >= next_power_sample_utc_) {
                power_ = board_.ReadPowerSnapshot();
                next_power_sample_utc_ = now + 60;
                rendered_revision_ = UINT32_MAX;
            }
            if (view_.revision != rendered_revision_) {
                Render(view_);
                rendered_revision_ = view_.revision;
            }
        }

        ZectrixButtonEvent event;
        if (board_.WaitButton(&event, pdMS_TO_TICKS(250))) {
            if (app_controller_get_view(&view_) == ESP_OK) {
                HandleButton(event, view_);
                Render(view_);
                rendered_revision_ = UINT32_MAX;
            }
        }
    }
}

void VibeNote4App::HandleButton(const ZectrixButtonEvent& event,
                                const app_controller_view_t& view) {
    if (event.button == ZectrixButton::kDown &&
        event.action == ZectrixButtonAction::kLongPress) {
        Shutdown();
        return;
    }
    if (event.action == ZectrixButtonAction::kLongPress) {
        if (event.button == ZectrixButton::kOk) {
            page_ = page_ == Page::kSettings ? Page::kOverview
                                              : Page::kSettings;
            confirm_yes_ = false;
        }
        return;
    }

    if (page_ == Page::kConfirmUnlink || page_ == Page::kConfirmReset) {
        if (event.button == ZectrixButton::kUp ||
            event.button == ZectrixButton::kDown) {
            confirm_yes_ = !confirm_yes_;
        } else if (event.button == ZectrixButton::kOk) {
            if (confirm_yes_) {
                app_controller_dispatch(page_ == Page::kConfirmUnlink
                                            ? APP_INTENT_UNLINK
                                            : APP_INTENT_FACTORY_RESET);
                page_ = Page::kOverview;
            } else {
                page_ = Page::kSettings;
            }
            confirm_yes_ = false;
        }
        return;
    }

    if (page_ == Page::kSettings) {
        if (event.button == ZectrixButton::kUp) {
            settings_index_ = settings_index_ == 0
                                  ? kSettingsCount - 1
                                  : settings_index_ - 1;
        } else if (event.button == ZectrixButton::kDown) {
            settings_index_ = (settings_index_ + 1) % kSettingsCount;
        } else {
            HandleSettingsClick(view);
        }
        return;
    }
    if (page_ == Page::kAbout) {
        if (event.button == ZectrixButton::kOk && about_view_ == VIBE_ABOUT_DETAILS) {
            page_ = Page::kSettings;
        } else {
            about_view_ = vibe_about_navigate(about_view_,
                event.button == ZectrixButton::kUp ? VIBE_ABOUT_UP :
                event.button == ZectrixButton::kDown ? VIBE_ABOUT_DOWN : VIBE_ABOUT_BACK);
        }
        return;
    }

    const Visual visual = ResolveVisual(view);
    if (visual == Visual::kWifi || visual == Visual::kLink ||
        visual == Visual::kTime || visual == Visual::kError) {
        if (event.button == ZectrixButton::kOk) {
            if (visual == Visual::kWifi) {
                app_controller_dispatch(view.wifi_provisioning
                                            ? APP_INTENT_RECONFIGURE_WIFI
                                            : APP_INTENT_START_WIFI);
            } else if (visual == Visual::kLink) {
                app_controller_dispatch(APP_INTENT_RELINK);
            } else {
                app_controller_dispatch(APP_INTENT_REFRESH);
            }
        }
        return;
    }

    if (event.button == ZectrixButton::kOk) {
        window_ = window_ == VIBE_WINDOW_TODAY ? VIBE_WINDOW_SEVEN_DAYS
                                                : VIBE_WINDOW_TODAY;
        agent_offset_ = 0;
    } else if (page_ == Page::kAgents) {
        const auto& snapshot = window_ == VIBE_WINDOW_TODAY ? view.today : view.seven_day;
        if (!vibe_list_step(&agent_offset_, snapshot.agent_count, 8,
                            event.button == ZectrixButton::kDown)) {
            page_ = event.button == ZectrixButton::kUp ? Page::kOverview : Page::kStatus;
            agent_offset_ = 0;
        }
    } else {
        int page = static_cast<int>(page_);
        page += event.button == ZectrixButton::kUp ? -1 : 1;
        if (page < 0) page = 2;
        if (page > 2) page = 0;
        page_ = static_cast<Page>(page);
        agent_offset_ = 0;
    }
}

void VibeNote4App::HandleSettingsClick(
    const app_controller_view_t& view) {
    switch (settings_index_) {
        case 0:
            app_controller_dispatch(APP_INTENT_REFRESH);
            page_ = Page::kOverview;
            break;
        case 1:
            app_controller_dispatch(APP_INTENT_RECONCILE);
            page_ = Page::kOverview;
            break;
        case 2:
            app_controller_dispatch(APP_INTENT_RECONFIGURE_WIFI);
            page_ = Page::kOverview;
            break;
        case 3:
            if (view.has_auth) {
                confirm_yes_ = false;
                page_ = Page::kConfirmUnlink;
            } else {
                app_controller_dispatch(APP_INTENT_RELINK);
                page_ = Page::kOverview;
            }
            break;
        case 4:
            app_controller_dispatch(APP_INTENT_TOGGLE_TIMEZONE);
            page_ = Page::kStatus;
            break;
        case 5:
            previous_valid_ = false;
            page_ = Page::kOverview;
            break;
        case 6:
            about_view_ = VIBE_ABOUT_DETAILS;
            page_ = Page::kAbout;
            break;
        case 7:
            confirm_yes_ = false;
            page_ = Page::kConfirmReset;
            break;
        case 8:
            app_controller_dispatch(APP_INTENT_CYCLE_LANGUAGE);
            previous_valid_ = false;
            break;
        default:
            break;
    }
}

void VibeNote4App::Shutdown() {
    canvas_.Clear();
    canvas_.TextCentered(92, TR("SHUTTING DOWN"), 2);
    canvas_.TextCentered(148, TR("Finishing storage and network work"), 1);
    canvas_.TextCentered(180, TR("Please wait..."), 1);
    CommitFrame(Visual::kShutdown, true);

    const esp_err_t stop_error = app_controller_prepare_shutdown(35000);
    if (stop_error != ESP_OK) {
        ESP_LOGW(kTag, "shutdown barrier: %s", esp_err_to_name(stop_error));
    }

    canvas_.Clear();
    CommitFrame(Visual::kShutdown, true);
    zectrix_epd_power_off(epd_);
    board_.SetPowerLed(false);
    board_.SetAudioPower(false);
    board_.PrepareDeepSleepHold();
    board_.CutBatteryPower();
    ESP_LOGI(kTag, "battery latch released; entering deep sleep fallback");
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();
}

VibeNote4App::Visual VibeNote4App::ResolveVisual(
    const app_controller_view_t& view) const {
    switch (page_) {
        case Page::kSettings: return Visual::kSettings;
        case Page::kAbout: return about_view_ == VIBE_ABOUT_DETAILS ? Visual::kAbout : Visual::kAboutQr;
        case Page::kConfirmUnlink: return Visual::kConfirmUnlink;
        case Page::kConfirmReset: return Visual::kConfirmReset;
        default: break;
    }
    switch (view.lifecycle) {
        case VIBE_APP_WIFI_REQUIRED:
        case VIBE_APP_WIFI_PROVISIONING:
            return Visual::kWifi;
        case VIBE_APP_WIFI_CONNECTING:
            return Visual::kConnecting;
        case VIBE_APP_TIME_SYNC:
        case VIBE_APP_TIME_REQUIRED:
            return Visual::kTime;
        case VIBE_APP_DEVICE_LINK:
            if (view.user_code[0] != '\0') return Visual::kLink;
            return view.reason == APP_REASON_NONE ? Visual::kConnecting
                                                   : Visual::kError;
        case VIBE_APP_STORAGE_ERROR:
            return Visual::kError;
        case VIBE_APP_BOOT:
        case VIBE_APP_LOAD_CONFIG:
        case VIBE_APP_RESETTING:
            return Visual::kConnecting;
        default:
            break;
    }
    if (view.reason != APP_REASON_NONE && !view.has_today &&
        view.lifecycle != VIBE_APP_SYNCING) {
        return Visual::kError;
    }
    switch (page_) {
        case Page::kAgents: return Visual::kAgents;
        case Page::kStatus: return Visual::kStatus;
        default: return Visual::kOverview;
    }
}

void VibeNote4App::Render(const app_controller_view_t& view,
                          bool force_full) {
    if (s_language != view.language) force_full = true;
    s_language = view.language;
    canvas_.Clear();
    qr_drawn_ = false;
    qr_hash_ = 0;
    const Visual visual = ResolveVisual(view);
    switch (visual) {
        case Visual::kOverview: RenderOverview(view); break;
        case Visual::kAgents: RenderAgents(view); break;
        case Visual::kStatus: RenderStatus(view); break;
        case Visual::kSettings: RenderSettings(view); break;
        case Visual::kAbout: RenderAbout(); break;
        case Visual::kAboutQr: RenderAbout(); break;
        case Visual::kConfirmUnlink:
            RenderConfirmation(TR("UNLINK VIBE ACCOUNT?"),
                               TR("Cached usage and API credentials are removed."));
            break;
        case Visual::kConfirmReset:
            RenderConfirmation(TR("FACTORY RESET?"),
                               TR("Wi-Fi, account, settings and cache are removed."));
            break;
        case Visual::kWifi: RenderWifi(view); break;
        case Visual::kLink: RenderLink(view); break;
        case Visual::kConnecting: RenderConnecting(view); break;
        case Visual::kTime: RenderTime(view); break;
        case Visual::kError: RenderError(view); break;
        case Visual::kShutdown: break;
    }
    const esp_err_t error = CommitFrame(visual, force_full);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "display refresh failed: %s", esp_err_to_name(error));
    }
}

void VibeNote4App::RenderHeader(const app_controller_view_t& view,
                                const char* title) {
    canvas_.Text(12, 8, title, 1);
    char right[48] = {};
    if (power_.battery_valid) {
        std::snprintf(right, sizeof(right), "%s  %u%%",
                      view.wifi_connected ? "Wi-Fi" : TR("Offline"),
                      power_.battery_percent);
    } else {
        std::snprintf(right, sizeof(right), "%s",
                      view.wifi_connected ? "Wi-Fi" : TR("Offline"));
    }
    canvas_.Text(388 - canvas_.TextWidth(right), 8, right, 1);
    canvas_.Line(12, 30, 387, 30);
}

void VibeNote4App::RenderOverview(const app_controller_view_t& view) {
    RenderHeader(view, TR("VIBE NOTE / OVERVIEW"));
    const vibe_usage_snapshot_t& snapshot =
        window_ == VIBE_WINDOW_TODAY ? view.today : view.seven_day;
    canvas_.Text(16, 42,
                 window_ == VIBE_WINDOW_TODAY ? TR("TODAY") : TR("LAST 7 DAYS"), 1);
    char tokens[32] = {};
    FormatTokens(SnapshotTokens(snapshot, window_), tokens, sizeof(tokens));
    canvas_.Text(16, 66, tokens, 3);
    canvas_.Text(16 + canvas_.TextWidth(tokens, 3) + 8, 90, TR("TOKENS"), 1);

    const uint8_t count = std::min<uint8_t>(snapshot.agent_count, 4);
    int y = 128;
    for (uint8_t i = 0; i < count; ++i) {
        const vibe_agent_usage_t& agent = snapshot.agents[i];
        char name[15] = {};
        char value[24] = {};
        SafeAgentName(agent.id, name, sizeof(name));
        FormatTokens(AgentTokens(agent, window_), value, sizeof(value));
        canvas_.Text(18, y, name, 1);
        canvas_.Text(382 - canvas_.TextWidth(value), y, value, 1);
        const int bar = static_cast<int>(AgentBasisPoints(agent, window_)) *
                        362 / 10000;
        /* A separate row keeps the bar outside both text bounding boxes. */
        canvas_.Rect(18, y + 18, 364, 7);
        if (bar > 0) canvas_.FillRect(19, y + 19, bar, 5, true);
        y += 29;
    }
    if (count == 0) {
        canvas_.TextCentered(154,
            snapshot.state == VIBE_EMPTY ? TR("No usage in this window")
                                         : TR("Usage has not synced yet"), 1);
    }

    char coverage[64] = {};
    std::snprintf(coverage, sizeof(coverage), TR("Coverage %u/7  %s%s"),
                  view.seven_day_coverage, DataName(snapshot.state),
                  snapshot.sources_collapsed ? TR("  +other") : "");
    canvas_.Text(16, 248, coverage, 1);
    DrawFooter(TR("UP prev"), TR("OK Today / 7D"), TR("DOWN next"));
}

void VibeNote4App::RenderAgents(const app_controller_view_t& view) {
    RenderHeader(view, TR("VIBE NOTE / AGENTS"));
    const vibe_usage_snapshot_t& snapshot =
        window_ == VIBE_WINDOW_TODAY ? view.today : view.seven_day;
    canvas_.Text(16, 40,
                 window_ == VIBE_WINDOW_TODAY ? TR("TODAY") : TR("LAST 7 DAYS"), 1);
    if (snapshot.agent_count == 0) {
        canvas_.TextCentered(128, TR("No agent usage available"), 1);
    }
    agent_offset_ = vibe_list_clamp(agent_offset_, snapshot.agent_count, 8);
    const uint8_t count = std::min<uint8_t>(snapshot.agent_count - agent_offset_, 8);
    int y = 65;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t index = agent_offset_ + i;
        const vibe_agent_usage_t& agent = snapshot.agents[index];
        char rank[4] = {};
        char name[19] = {};
        char value[24] = {};
        char percent[12] = {};
        std::snprintf(rank, sizeof(rank), "%u", index + 1);
        SafeAgentName(agent.id, name, sizeof(name));
        /* Font is proportional: byte counts alone cannot bound a column. */
        if (canvas_.TextWidth(name) > 184) {
            size_t length = std::strlen(name);
            while (length > 0 && canvas_.TextWidth(name) + canvas_.TextWidth("...") > 184) {
                name[--length] = '\0';
            }
            std::strncat(name, "...", sizeof(name) - std::strlen(name) - 1);
        }
        FormatTokens(AgentTokens(agent, window_), value, sizeof(value));
        const uint16_t bp = AgentBasisPoints(agent, window_);
        std::snprintf(percent, sizeof(percent), "%u.%u%%", bp / 100,
                      (bp % 100) / 10);
        canvas_.Text(16, y, rank, 1);
        canvas_.Text(42, y, name, 1);
        canvas_.Text(288 - canvas_.TextWidth(percent), y, percent, 1);
        canvas_.Text(384 - canvas_.TextWidth(value), y, value, 1);
        y += 24;
    }
    if (snapshot.agent_count > 8) {
        char range[48] = {};
        std::snprintf(range, sizeof(range), TR("Sources %u-%u of %u"),
                      agent_offset_ + 1, agent_offset_ + count, snapshot.agent_count);
        canvas_.Text(16, 258, range, 1);
    }
    DrawFooter(TR("UP prev"), TR("OK Today / 7D"), TR("DOWN next"));
}

void VibeNote4App::RenderStatus(const app_controller_view_t& view) {
    RenderHeader(view, TR("VIBE NOTE / STATUS"));
    char line[96] = {};
    int y = 42;
    std::snprintf(line, sizeof(line), TR("System       %s%s"), StateName(view.lifecycle),
                  view.busy ? TR(" (busy)") : "");
    canvas_.Text(18, y, line, 1); y += 25;
    std::snprintf(line, sizeof(line), TR("Wi-Fi       %s  saved=%s"),
                  view.wifi_connected ? TR("connected") : TR("offline"),
                  view.wifi_has_credentials ? TR("yes") : TR("no"));
    canvas_.Text(18, y, line, 1); y += 25;
    std::snprintf(line, sizeof(line), TR("Account     %s"), view.has_auth ? TR("linked") : TR("not linked"));
    canvas_.Text(18, y, line, 1); y += 25;
    std::snprintf(line, sizeof(line), TR("Timezone    %s"), vibe_timezone_name(view.timezone));
    canvas_.Text(18, y, line, 1); y += 25;
    char synced[32] = {};
    FormatTime(view.today.last_fetch_at, view.timezone, synced, sizeof(synced));
    std::snprintf(line, sizeof(line), TR("Last sync   %s"), synced);
    canvas_.Text(18, y, line, 1); y += 25;
    std::snprintf(line, sizeof(line), TR("Metric      API total v1   coverage %u/7"),
                  view.seven_day_coverage);
    canvas_.Text(18, y, line, 1); y += 25;
    std::snprintf(line, sizeof(line), TR("Data        Today:%s  7D:%s"), DataName(view.data_state),
                  DataName(view.seven_day.state));
    canvas_.Text(18, y, line, 1); y += 25;
    std::snprintf(line, sizeof(line), TR("Firmware    %s   device %s"),
                  esp_app_get_description()->version, view.device_id);
    canvas_.Text(18, y, line, 1); y += 25;
    if (power_.battery_valid) {
        std::snprintf(line, sizeof(line), TR("Battery     %u%%  %umV%s"),
                      power_.battery_percent, power_.battery_mv,
                      power_.charge.charging ? TR(" charging") : "");
        canvas_.Text(18, y, line, 1);
    }
    DrawFooter(TR("UP prev"), TR("OK Today / 7D"), TR("DOWN next"));
}

void VibeNote4App::RenderSettings(const app_controller_view_t& view) {
    RenderHeader(view, TR("SETTINGS"));
    for (uint8_t index = 0; index < kSettingsCount; ++index) {
        const int y = 40 + index * 25;
        if (index == settings_index_) {
            canvas_.FillRect(12, y - 3, 376, 22, true);
        }
        char item[64] = {};
        if (index == 4) {
            std::snprintf(item, sizeof(item), "%s: %s", TR(kSettings[index]),
                          vibe_timezone_name(view.timezone));
        } else if (index == 3) {
            std::snprintf(item, sizeof(item), "%s: %s", TR(kSettings[index]),
                          view.has_auth ? TR("linked") : TR("not linked"));
        } else if (index == 8) {
            std::snprintf(item, sizeof(item), "%s: %s", TR(kSettings[index]), vibe_language_name(view.language));
        } else {
            std::snprintf(item, sizeof(item), "%s", TR(kSettings[index]));
        }
        canvas_.Text(20, y, item, 1, index == settings_index_);
    }
    DrawFooter("UP", TR(std::strcmp(view.detail, "Could not save language") == 0
                           ? "Save failed" : "OK select / hold back"), "DOWN");
}

void VibeNote4App::RenderAbout() {
    if (about_view_ != VIBE_ABOUT_DETAILS) {
        canvas_.TextCentered(10, TR(vibe_about_title(about_view_)), 2);
        DrawQr(vibe_about_url(about_view_), 100, 48, 200);
        canvas_.TextCentered(252, TR("Scan to visit."), 1);
        DrawFooter(TR("UP Repo"), TR("OK back"), "DOWN X");
        return;
    }
    canvas_.TextCentered(28, VIBE_NOTE_ABOUT, 2);
    canvas_.Line(24, 68, 375, 68);
    char version[64] = {};
    std::snprintf(version, sizeof(version), TR("Version %s"),
                  esp_app_get_description()->version);
    canvas_.TextCentered(86, version, 1);
    canvas_.TextCentered(126, TR("Repository"), 1);
    canvas_.TextCentered(150, VIBE_REPOSITORY_URL, 1);
    canvas_.TextCentered(198, TR("Author:"), 1);
    canvas_.TextCentered(222, VIBE_AUTHOR_URL, 1);
    DrawFooter(TR("UP Repo"), TR("OK back"), "DOWN X");
}

void VibeNote4App::RenderConfirmation(const char* title,
                                      const char* detail) {
    canvas_.TextCentered(54, title, 2);
    canvas_.TextCentered(112, detail, 1);
    canvas_.TextCentered(150, "This action cannot be undone on the device.", 1);
    if (confirm_yes_) {
        canvas_.Rect(92, 196, 90, 40);
        canvas_.FillRect(218, 196, 90, 40, true);
    } else {
        canvas_.FillRect(92, 196, 90, 40, true);
        canvas_.Rect(218, 196, 90, 40);
    }
    canvas_.Text(116, 207, TR("NO"), 1, !confirm_yes_);
    canvas_.Text(244, 207, TR("YES"), 1, confirm_yes_);
    DrawFooter(TR("UP toggle"), TR("OK confirm"), TR("DOWN toggle"));
}

void VibeNote4App::RenderWifi(const app_controller_view_t& view) {
    canvas_.Text(14, 10, TR("WI-FI SETUP"), 2);
    char payload[80] = {};
    std::snprintf(payload, sizeof(payload), "WIFI:T:nopass;S:%s;;",
                  view.ap_ssid);
    DrawQr(payload, 14, 54, 210);
    canvas_.Text(240, 62, TR("1. Connect to"), 1);
    canvas_.Text(240, 86, view.ap_ssid[0] == '\0' ? "VibeNote-XXXX" : view.ap_ssid, 1);
    canvas_.Text(240, 122, TR("2. Open"), 1);
    canvas_.Text(240, 146, view.portal_url[0] == '\0'
                                      ? "http://192.168.4.1"
                                      : view.portal_url, 1);
    canvas_.Text(240, 182, TR("3. Save Wi-Fi"), 1);
    canvas_.Text(240, 214, TR("Portal closes"), 1);
    canvas_.Text(240, 236, TR("after 10 min"), 1);
    DrawFooter("", TR("OK restart setup"), "");
}

void VibeNote4App::RenderLink(const app_controller_view_t& view) {
    canvas_.Text(14, 10, TR("LINK VIBE ACCOUNT"), 2);
    DrawQr(view.verification_uri, 14, 54, 210);
    canvas_.Text(240, 64, TR("Scan, then enter:"), 1);
    canvas_.Text(240, 98, view.user_code, 2);
    canvas_.Text(240, 150, TR("No password"), 1);
    canvas_.Text(240, 174, TR("on this device."), 1);
    int64_t remaining = view.link_deadline_monotonic - view.link_now_monotonic;
    if (remaining < 0) remaining = 0;
    char expires[48] = {};
    std::snprintf(expires, sizeof(expires), TR("Expires in %u min"),
                  (unsigned)((remaining + 59) / 60));
    canvas_.Text(240, 214, expires, 1);
    DrawFooter("", TR("OK request new code"), "");
}

void VibeNote4App::RenderConnecting(const app_controller_view_t& view) {
    canvas_.TextCentered(46, VIBE_NOTE_ABOUT, 2);
    canvas_.Line(36, 88, 363, 88);
    canvas_.TextCentered(124, StateName(view.lifecycle), 2);
    canvas_.TextCentered(174,
        view.detail[0] == '\0' ? TR("Preparing the dashboard...") : TR(view.detail), 1);
    canvas_.TextCentered(218, TR("Saved data remains available after reboot"), 1);
    DrawFooter("", TR("Hold OK for settings"), "");
}

void VibeNote4App::RenderTime(const app_controller_view_t& view) {
    canvas_.TextCentered(50, TR("SETTING THE CLOCK"), 2);
    canvas_.TextCentered(112,
        view.lifecycle == VIBE_APP_TIME_SYNC ? TR("Synchronizing securely over NTP")
                                             : TR("A valid clock is required"), 1);
    canvas_.TextCentered(150, TR("Cached data is not assigned to a guessed date"), 1);
    canvas_.TextCentered(188, TR(view.detail), 1);
    DrawFooter("", TR("OK retry"), "");
}

void VibeNote4App::RenderError(const app_controller_view_t& view) {
    canvas_.TextCentered(38, TR("ACTION NEEDED"), 2);
    char reason[80] = {};
    std::snprintf(reason, sizeof(reason), TR("Problem: %s"), ReasonName(view.reason));
    canvas_.TextCentered(94, reason, 1);
    canvas_.TextCentered(126, view.detail[0] == '\0' ? StateName(view.lifecycle)
                                                     : TR(view.detail), 1);
    char diagnostics[80] = {};
    std::snprintf(diagnostics, sizeof(diagnostics), "HTTP %d   system 0x%x   %s",
                  view.last_http_status,
                  static_cast<unsigned int>(view.last_system_error),
                  vibe_error_name(view.last_error));
    canvas_.TextCentered(164, diagnostics, 1);
    canvas_.TextCentered(208, view.has_today
                                  ? TR("Last known good data is still available")
                                  : TR("No trusted usage data is available yet"), 1);
    DrawFooter("", TR("OK retry / hold OK settings"), "");
}

void VibeNote4App::DrawQrCallback(esp_qrcode_handle_t qrcode,
                                  void* context) {
    QrDrawContext* draw = static_cast<QrDrawContext*>(context);
    if (draw == nullptr || draw->canvas == nullptr) {
        return;
    }
    draw->drawn = note_draw_qr(*draw->canvas, qrcode, draw->x, draw->y, draw->extent);
}

bool VibeNote4App::DrawQr(const char* text, int x, int y, int extent) {
    if (text == nullptr || text[0] == '\0') {
        canvas_.Rect(x, y, extent, extent);
        canvas_.Text(x + 18, y + extent / 2 - 8, TR("QR unavailable"), 1);
        return false;
    }
    uint32_t hash = 2166136261U;
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(text);
         *cursor != '\0'; ++cursor) {
        hash ^= *cursor;
        hash *= 16777619U;
    }
    QrDrawContext context = {.canvas = &canvas_, .x = x, .y = y,
                             .extent = extent, .drawn = false};
    esp_qrcode_config_t config = {};
    config.display_func_with_cb = DrawQrCallback;
    config.max_qrcode_version = 15;
    config.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
    config.user_data = &context;
    const esp_err_t error = esp_qrcode_generate(&config, text);
    if (error != ESP_OK || !context.drawn) {
        ESP_LOGW(kTag, "QR generation failed: %s", esp_err_to_name(error));
        canvas_.Rect(x, y, extent, extent);
        canvas_.Text(x + 18, y + extent / 2 - 8, TR("QR unavailable"), 1);
        return false;
    }
    qr_drawn_ = true;
    qr_hash_ = hash;
    return true;
}

esp_err_t VibeNote4App::CommitFrame(Visual visual, bool force_full) {
    const bool layout_changed = !previous_valid_ || visual != previous_visual_;
    const bool qr_changed = qr_drawn_ && qr_hash_ != previous_qr_hash_;
    note4_epd_refresh_plan_t plan = {};
    if (!note4_epd_plan_refresh(
            canvas_.data(), previous_.data(), canvas_.size(),
            ZectrixCanvas::kWidth, ZectrixCanvas::kHeight,
            ZectrixCanvas::kStride, previous_valid_, force_full,
            layout_changed, qr_changed, partial_count_, &plan)) {
        previous_valid_ = false;
        return ESP_ERR_INVALID_ARG;
    }
    if (plan.action == NOTE4_EPD_REFRESH_SKIP) return ESP_OK;
    const bool full = plan.action == NOTE4_EPD_REFRESH_FULL;

    esp_err_t error = zectrix_epd_power_on(epd_);
    if (error != ESP_OK) {
        previous_valid_ = false;
        return error;
    }
    if (full) {
        error = zectrix_epd_refresh_full_1bpp(epd_, canvas_.data(),
                                              canvas_.size());
    } else {
        const size_t min_byte = plan.x / 8U;
        for (uint16_t row = 0; row < plan.height; ++row) {
            const uint8_t* source = canvas_.data() +
                static_cast<size_t>(plan.y + row) * ZectrixCanvas::kStride +
                min_byte;
            std::memcpy(dirty_.data() + static_cast<size_t>(row) * plan.row_bytes,
                        source, plan.row_bytes);
        }
        const zectrix_epd_rect_t rect = {
            .x = plan.x,
            .y = plan.y,
            .width = plan.width,
            .height = plan.height,
        };
        error = zectrix_epd_refresh_partial_1bpp(
            epd_, &rect, dirty_.data(), plan.row_bytes * plan.height);
    }
    const esp_err_t off_error = zectrix_epd_power_off(epd_);
    if (error == ESP_OK) {
        error = off_error;
    }
    if (error != ESP_OK) {
        previous_valid_ = false;
        return error;
    }

    std::memcpy(previous_.data(), canvas_.data(), canvas_.size());
    previous_valid_ = true;
    previous_visual_ = visual;
    previous_qr_hash_ = qr_drawn_ ? qr_hash_ : 0;
    if (full) {
        partial_count_ = 0;
        ESP_LOGI(kTag, "full display refresh visual=%u",
                 static_cast<unsigned int>(visual));
    } else {
        ++partial_count_;
        ESP_LOGI(kTag, "partial display refresh x=%d y=%d w=%d h=%d count=%u",
                 plan.x, plan.y, plan.width, plan.height, partial_count_);
    }
    return ESP_OK;
}

void VibeNote4App::DrawFooter(const char* left, const char* center,
                              const char* right) {
    canvas_.Line(12, 268, 387, 268);
    canvas_.Text(14, kFooterY, left, 1);
    canvas_.TextCentered(kFooterY, center, 1);
    canvas_.Text(386 - canvas_.TextWidth(right), kFooterY, right, 1);
}
