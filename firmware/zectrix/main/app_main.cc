#include "device_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "vibe_client.h"
#include "vibe_note4_ui.h"

namespace {
constexpr char kTag[] = "vibe_note4";
VibeNote4App s_app;
}

extern "C" void app_main(void) {
    const esp_err_t latch_error = ZectrixHoldBatteryPowerEarly();
    if (latch_error != ESP_OK) {
        ESP_LOGE(kTag, "battery self-hold failed: %s",
                 esp_err_to_name(latch_error));
        return;
    }
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
    esp_log_level_set("QRCODE", ESP_LOG_WARN);
    ESP_LOGI(kTag, "vibe-usage-esp32 NOTE4 boot");
    const esp_err_t nvs_error = nvs_flash_init();
    if (nvs_error != ESP_OK) {
        ESP_LOGE(kTag, "NVS unavailable; refusing destructive recovery: %s",
                 esp_err_to_name(nvs_error));
        return;
    }

    bool reset_pending = false;
    esp_err_t error = vibe_factory_reset_pending(&reset_pending);
    if (error == ESP_OK && reset_pending) {
        error = vibe_factory_reset_resume_local();
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "reset recovery unavailable: %s",
                 esp_err_to_name(error));
        return;
    }

    device_config_t settings = {};
    error = device_config_load_or_create(1800, &settings);
    if (error == ESP_OK) {
        error = s_app.Run(settings);
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "application stopped during initialization: %s",
                 esp_err_to_name(error));
    }
}
