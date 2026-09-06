#include "wifi_adapter.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "esp_log.h"
#include "esp_timer.h"
#include "ssid_manager.h"
#include "wifi_manager.h"
#include "cJSON.h"

namespace {

constexpr char kTag[] = "wifi_adapter";
constexpr char kPortalUrl[] = "http://192.168.4.1";

bool initialized = false;
char ap_ssid[33] = {};
wifi_adapter_event_cb_t callback = nullptr;
void *callback_context = nullptr;
esp_timer_handle_t portal_timer = nullptr;
uint64_t portal_timeout_us = 600ULL * 1000000ULL;

void Notify(wifi_adapter_event_t event) {
    if (callback != nullptr) callback(event, callback_context);
}

void PortalTimeout(void *) {
    ESP_LOGI(kTag, "Provisioning window expired");
    Notify(WIFI_ADAPTER_PROVISIONING_TIMEOUT);
}

wifi_adapter_event_t Translate(WifiEvent event) {
    switch (event) {
        case WifiEvent::Scanning: return WIFI_ADAPTER_SCANNING;
        case WifiEvent::Connecting: return WIFI_ADAPTER_CONNECTING;
        case WifiEvent::Connected: return WIFI_ADAPTER_GOT_IP;
        case WifiEvent::Disconnected: return WIFI_ADAPTER_DISCONNECTED;
        case WifiEvent::ConfigModeEnter: return WIFI_ADAPTER_PORTAL_ENTER;
        case WifiEvent::ConfigModeExit: return WIFI_ADAPTER_PORTAL_EXIT;
    }
    return WIFI_ADAPTER_DISCONNECTED;
}

}  // namespace

extern "C" esp_err_t wifi_adapter_init(const wifi_adapter_config_t *config) {
    if (config == nullptr || config->product_prefix == nullptr ||
        config->device_id == nullptr || config->station_hostname == nullptr ||
        config->product_prefix[0] == '\0' || config->device_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (initialized) return ESP_OK;

    const int length = std::snprintf(ap_ssid, sizeof(ap_ssid), "%s-%s",
                                     config->product_prefix,
                                     config->device_id);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(ap_ssid)) {
        return ESP_ERR_INVALID_SIZE;
    }
    callback = config->event_callback;
    callback_context = config->event_context;
    if (config->provisioning_timeout_seconds != 0) {
        portal_timeout_us =
            static_cast<uint64_t>(config->provisioning_timeout_seconds) *
            1000000ULL;
    }

    WifiManagerConfig manager_config;
    manager_config.ap_ssid = ap_ssid;
    manager_config.ssid_prefix = config->product_prefix;
    manager_config.station_hostname = config->station_hostname;
    manager_config.language = config->language ? config->language : "en-US";
    cJSON *info = cJSON_CreateObject();
    if (!info) return ESP_ERR_NO_MEM;
    bool added = cJSON_AddStringToObject(info, "product", config->product_name ? config->product_name : "Vibe") &&
        cJSON_AddStringToObject(info, "version", config->firmware_version ? config->firmware_version : "") &&
        cJSON_AddStringToObject(info, "repository", config->repository_url ? config->repository_url : "") &&
        cJSON_AddStringToObject(info, "author", config->author_url ? config->author_url : "");
    char *json = added ? cJSON_PrintUnformatted(info) : nullptr;
    cJSON_Delete(info);
    if (!json) return ESP_ERR_NO_MEM;
    manager_config.project_info = json;
    cJSON_free(json);
    manager_config.show_ota_config = false;
    manager_config.show_sleep_config = false;
    auto &manager = WifiManager::GetInstance();
    if (!manager.Initialize(manager_config)) return ESP_FAIL;
    manager.SetEventCallback([](WifiEvent event, const std::string &) {
        if (event == WifiEvent::ConfigModeExit && portal_timer != nullptr) {
            esp_timer_stop(portal_timer);
        }
        Notify(Translate(event));
    });

    const esp_timer_create_args_t timer_args = {
        .callback = PortalTimeout,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vibe_portal",
        .skip_unhandled_events = true,
    };
    esp_err_t error = esp_timer_create(&timer_args, &portal_timer);
    if (error != ESP_OK) return error;
    initialized = true;
    return ESP_OK;
}

extern "C" bool wifi_adapter_has_credentials(void) {
    return !SsidManager::GetInstance().GetSsidList().empty();
}

extern "C" void wifi_adapter_set_language(const char *language) {
    if (language) WifiManager::GetInstance().SetLanguage(language);
}

extern "C" size_t wifi_adapter_credential_count(void) {
    return SsidManager::GetInstance().GetSsidList().size();
}

extern "C" esp_err_t wifi_adapter_start_station(void) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (!wifi_adapter_has_credentials()) return ESP_ERR_NOT_FOUND;
    WifiManager::GetInstance().StartStation();
    return ESP_OK;
}

extern "C" void wifi_adapter_stop_station(void) {
    if (initialized) WifiManager::GetInstance().StopStation();
}

extern "C" esp_err_t wifi_adapter_start_provisioning(void) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (portal_timer != nullptr) {
        esp_timer_stop(portal_timer);
        esp_err_t error = esp_timer_start_once(portal_timer, portal_timeout_us);
        if (error != ESP_OK) return error;
    }
    WifiManager::GetInstance().StartConfigAp();
    return ESP_OK;
}

extern "C" void wifi_adapter_stop_provisioning(void) {
    if (!initialized) return;
    if (portal_timer != nullptr) esp_timer_stop(portal_timer);
    WifiManager::GetInstance().StopConfigAp();
}

extern "C" bool wifi_adapter_is_connected(void) {
    return initialized && WifiManager::GetInstance().IsConnected();
}

extern "C" bool wifi_adapter_is_provisioning(void) {
    return initialized && WifiManager::GetInstance().IsConfigMode();
}

extern "C" const char *wifi_adapter_ap_ssid(void) { return ap_ssid; }

extern "C" const char *wifi_adapter_portal_url(void) { return kPortalUrl; }

extern "C" esp_err_t wifi_adapter_station_ip(char *buffer, size_t capacity) {
    if (buffer == nullptr || capacity == 0) return ESP_ERR_INVALID_ARG;
    buffer[0] = '\0';
    if (!wifi_adapter_is_connected()) return ESP_ERR_INVALID_STATE;
    const std::string ip = WifiManager::GetInstance().GetIpAddress();
    if (ip.empty() || ip == "0.0.0.0") return ESP_ERR_INVALID_STATE;
    if (ip.size() >= capacity) return ESP_ERR_INVALID_SIZE;
    std::memcpy(buffer, ip.c_str(), ip.size() + 1);
    return ESP_OK;
}

extern "C" esp_err_t wifi_adapter_remove_credential(size_t index) {
    auto &credentials = SsidManager::GetInstance().GetSsidList();
    if (index >= credentials.size()) return ESP_ERR_INVALID_ARG;
    return SsidManager::GetInstance().RemoveSsid(static_cast<int>(index));
}

extern "C" esp_err_t wifi_adapter_clear_credentials(void) {
    return SsidManager::GetInstance().Clear();
}
