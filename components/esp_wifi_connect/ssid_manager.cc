#include "ssid_manager.h"

#include <algorithm>
#include <cstring>
#include <esp_log.h>
#include <nvs_flash.h>

#define TAG "SsidManager"
#define NVS_NAMESPACE "wifi"
#define MAX_WIFI_SSID_COUNT 10

SsidManager::SsidManager() {
    LoadFromNvs();
}

SsidManager::~SsidManager() {
}

esp_err_t SsidManager::Clear() {
    const auto previous = ssid_list_;
    ssid_list_.clear();
    const esp_err_t error = SaveToNvs();
    if (error != ESP_OK) ssid_list_ = previous;
    return error;
}

void SsidManager::LoadFromNvs() {
    ssid_list_.clear();

    // Load ssid and password from NVS from namespace "wifi"
    // ssid, ssid1, ssid2, ... ssid9
    // password, password1, password2, ... password9
    nvs_handle_t nvs_handle;
    auto ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        // The namespace doesn't exist, just return
        ESP_LOGW(TAG, "NVS namespace %s doesn't exist", NVS_NAMESPACE);
        return;
    }
    for (int i = 0; i < MAX_WIFI_SSID_COUNT; i++) {
        std::string ssid_key = "ssid";
        if (i > 0) {
            ssid_key += std::to_string(i);
        }
        std::string password_key = "password";
        if (i > 0) {
            password_key += std::to_string(i);
        }
        
        char ssid[33];
        char password[65];
        size_t length = sizeof(ssid);
        if (nvs_get_str(nvs_handle, ssid_key.c_str(), ssid, &length) != ESP_OK) {
            continue;
        }
        length = sizeof(password);
        if (nvs_get_str(nvs_handle, password_key.c_str(), password, &length) != ESP_OK) {
            continue;
        }
        const size_t ssid_length = strnlen(ssid, sizeof(ssid));
        const size_t password_length = strnlen(password, sizeof(password));
        if (ssid_length == 0 || ssid_length > 32U ||
            password_length > 64U) {
            ESP_LOGW(TAG, "Ignoring invalid stored Wi-Fi credential");
            continue;
        }
        ssid_list_.push_back({ssid, password});
    }
    nvs_close(nvs_handle);
}

esp_err_t SsidManager::SaveToNvs() {
    nvs_handle_t nvs_handle;
    esp_err_t error = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (error != ESP_OK) return error;
    for (int i = 0; i < MAX_WIFI_SSID_COUNT; i++) {
        std::string ssid_key = "ssid";
        if (i > 0) {
            ssid_key += std::to_string(i);
        }
        std::string password_key = "password";
        if (i > 0) {
            password_key += std::to_string(i);
        }
        
        if (error != ESP_OK) break;
        if (i < static_cast<int>(ssid_list_.size())) {
            error = nvs_set_str(nvs_handle, ssid_key.c_str(),
                                ssid_list_[i].ssid.c_str());
            if (error == ESP_OK) {
                error = nvs_set_str(nvs_handle, password_key.c_str(),
                                    ssid_list_[i].password.c_str());
            }
        } else {
            esp_err_t item_error = nvs_erase_key(nvs_handle,
                                                 ssid_key.c_str());
            if (item_error != ESP_OK && item_error != ESP_ERR_NVS_NOT_FOUND) {
                error = item_error;
            }
            item_error = nvs_erase_key(nvs_handle, password_key.c_str());
            if (error == ESP_OK && item_error != ESP_OK &&
                item_error != ESP_ERR_NVS_NOT_FOUND) {
                error = item_error;
            }
        }
    }
    if (error == ESP_OK) error = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return error;
}

esp_err_t SsidManager::AddSsid(const std::string& ssid,
                               const std::string& password) {
    if (ssid.empty() || ssid.size() > 32U || password.size() > 64U) {
        return ESP_ERR_INVALID_ARG;
    }
    const auto previous = ssid_list_;
    for (auto& item : ssid_list_) {
        if (item.ssid == ssid) {
            ESP_LOGI(TAG, "Updating an existing Wi-Fi credential");
            item.password = password;
            const esp_err_t error = SaveToNvs();
            if (error != ESP_OK) ssid_list_ = previous;
            return error;
        }
    }

    if (ssid_list_.size() >= MAX_WIFI_SSID_COUNT) {
        ESP_LOGW(TAG, "SSID list is full, pop one");
        ssid_list_.pop_back();
    }
    // Add the new ssid to the front of the list
    ssid_list_.insert(ssid_list_.begin(), {ssid, password});
    const esp_err_t error = SaveToNvs();
    if (error != ESP_OK) ssid_list_ = previous;
    return error;
}

esp_err_t SsidManager::RemoveSsid(int index) {
    if (index < 0 || index >= static_cast<int>(ssid_list_.size())) {
        ESP_LOGW(TAG, "Invalid index %d", index);
        return ESP_ERR_INVALID_ARG;
    }
    const auto previous = ssid_list_;
    ssid_list_.erase(ssid_list_.begin() + index);
    const esp_err_t error = SaveToNvs();
    if (error != ESP_OK) ssid_list_ = previous;
    return error;
}

esp_err_t SsidManager::SetDefaultSsid(int index) {
    if (index < 0 || index >= static_cast<int>(ssid_list_.size())) {
        ESP_LOGW(TAG, "Invalid index %d", index);
        return ESP_ERR_INVALID_ARG;
    }
    const auto previous = ssid_list_;
    // Move the ssid at index to the front of the list
    auto item = ssid_list_[index];
    ssid_list_.erase(ssid_list_.begin() + index);
    ssid_list_.insert(ssid_list_.begin(), item);
    const esp_err_t error = SaveToNvs();
    if (error != ESP_OK) ssid_list_ = previous;
    return error;
}
