#ifndef VIBE_NOTE4_UI_H_
#define VIBE_NOTE4_UI_H_

#include <array>
#include <cstdint>

#include "app_controller.h"
#include "vibe_about.h"
#include "esp_err.h"
#include "qrcode.h"
#include "zectrix_board.h"
#include "zectrix_canvas.h"
#include "zectrix_epd.h"

class VibeNote4App {
public:
    esp_err_t Run(const device_config_t& settings);

private:
    enum class Page : uint8_t {
        kOverview = 0,
        kAgents,
        kStatus,
        kTodo,
        kSettings,
        kAbout,
        kLlmConfig,
        kConfirmUnlink,
        kConfirmReset,
    };

    enum class Visual : uint8_t {
        kOverview = 0,
        kAgents,
        kStatus,
        kTodo,
        kSettings,
        kAbout,
        kLlmConfig,
        kConfirmUnlink,
        kConfirmReset,
        kWifi,
        kLink,
        kConnecting,
        kTime,
        kError,
        kShutdown,
        kAboutQr,
    };

    static void OkReleased(void* context);
    static void TimeSynchronized(int64_t utc_seconds, void* context);
    static void DrawQrCallback(esp_qrcode_handle_t qrcode, void* context);

    bool RestoreTimeFromRtc(vibe_timezone_t timezone);
    void StoreTimeInRtc(int64_t utc_seconds);
    bool HeldAtBoot(ZectrixButton button, uint32_t duration_ms);
    void Loop();
    void HandleButton(const ZectrixButtonEvent& event,
                      const app_controller_view_t& view);
    void HandleSettingsClick(const app_controller_view_t& view);
    void Shutdown();

    Visual ResolveVisual(const app_controller_view_t& view) const;
    bool Render(const app_controller_view_t& view, bool force_full = false);
    void RenderHeader(const app_controller_view_t& view, const char* title);
    void RenderOverview(const app_controller_view_t& view);
    void RenderAgents(const app_controller_view_t& view);
    void RenderStatus(const app_controller_view_t& view);
    void RenderTodo(const app_controller_view_t& view);
    void RenderLlmConfig(const app_controller_view_t& view);
    void RenderSettings(const app_controller_view_t& view);
    void RenderAbout();
    void RenderConfirmation(const char* title, const char* detail);
    void RenderWifi(const app_controller_view_t& view);
    void RenderLink(const app_controller_view_t& view);
    void RenderConnecting(const app_controller_view_t& view);
    void RenderTime(const app_controller_view_t& view);
    void RenderError(const app_controller_view_t& view);
    bool DrawQr(const char* text, int x, int y, int extent);
    esp_err_t CommitFrame(Visual visual, bool force_full);
    void DrawFooter(const char* left, const char* center, const char* right);

    ZectrixBoard board_;
    zectrix_epd_handle_t epd_ = nullptr;
    ZectrixCanvas canvas_;
    std::array<uint8_t, ZECTRIX_EPD_1BPP_FRAME_BYTES> previous_ = {};
    std::array<uint8_t, ZECTRIX_EPD_1BPP_FRAME_BYTES> dirty_ = {};
    bool previous_valid_ = false;
    bool qr_drawn_ = false;
    uint32_t qr_hash_ = 0;
    uint32_t previous_qr_hash_ = 0;
    bool confirm_yes_ = false;
    uint8_t partial_count_ = 0;
    uint8_t settings_index_ = 0;
    vibe_about_view_t about_view_ = VIBE_ABOUT_DETAILS;
    uint8_t agent_offset_ = 0;
    uint8_t todo_offset_ = 0;
    uint8_t wifi_qr_step_ = 0;
    bool voice_held_ = false;
    uint32_t shown_alert_id_ = 0;
    uint32_t shown_alert_sequence_ = 0;
    uint32_t rendered_revision_ = UINT32_MAX;
    int64_t next_power_sample_utc_ = 0;
    ZectrixPowerSnapshot power_ = {};
    app_controller_view_t view_ = {};
    Page page_ = Page::kOverview;
    Visual previous_visual_ = Visual::kShutdown;
    vibe_window_t window_ = VIBE_WINDOW_TODAY;
};

#endif  // VIBE_NOTE4_UI_H_
