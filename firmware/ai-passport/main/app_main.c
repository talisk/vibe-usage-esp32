#include "device_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "vibe_client.h"
#include "vibe_passport_ui.h"

static const char *TAG = "vibe_passport";

void app_main(void) {
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
    ESP_LOGI(TAG, "vibe-usage-esp32 Passport boot");
    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable; refusing destructive recovery: %s",
                 esp_err_to_name(error));
        return;
    }
    bool reset_pending = false;
    error = vibe_factory_reset_pending(&reset_pending);
    if (error == ESP_OK && reset_pending) {
        error = vibe_factory_reset_resume_local();
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Reset recovery unavailable: %s",
                 esp_err_to_name(error));
        return;
    }
    device_config_t settings;
    error = device_config_load_or_create(900, &settings);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Settings unavailable: %s", esp_err_to_name(error));
        return;
    }
    error = vibe_passport_run(&settings);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Application initialization failed: %s",
                 esp_err_to_name(error));
    }
}
