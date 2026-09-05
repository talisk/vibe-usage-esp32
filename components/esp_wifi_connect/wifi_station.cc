#include "wifi_station.h"
#include <cstring>
#include <algorithm>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs.h>
#include "nvs_flash.h"
#include <esp_netif.h>
#include <esp_system.h>
#include "ssid_manager.h"

#define TAG "WifiStation"
#define WIFI_EVENT_CONNECTED BIT0
#define WIFI_EVENT_STOPPED BIT1
#define WIFI_EVENT_SCAN_DONE_BIT BIT2
#define MAX_RECONNECT_COUNT 5

WifiStation::WifiStation() {
    // Create the event group
    event_group_ = xEventGroupCreate();

    // 读取配置
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        max_tx_power_ = 0;
        remember_bssid_ = 0;
    } else {
        err = nvs_get_i8(nvs, "max_tx_power", &max_tx_power_);
        if (err != ESP_OK) {
            max_tx_power_ = 0;
        }
        err = nvs_get_u8(nvs, "remember_bssid", &remember_bssid_);
        if (err != ESP_OK) {
            remember_bssid_ = 0;
        }
        nvs_close(nvs);
    }
}

WifiStation::~WifiStation() {
    Stop();
    if (event_group_) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
}

void WifiStation::AddAuth(const std::string &&ssid, const std::string &&password) {
    auto& ssid_manager = SsidManager::GetInstance();
    ssid_manager.AddSsid(ssid, password);
}

void WifiStation::Stop() {
    ESP_LOGI(TAG, "Stopping WiFi station");
    
    // Unregister event handlers FIRST to prevent scan done from triggering connect
    if (instance_any_id_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_);
        instance_any_id_ = nullptr;
    }
    if (instance_got_ip_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_);
        instance_got_ip_ = nullptr;
    }

    // Stop timer
    if (timer_handle_ != nullptr) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    }

    // Now safe to stop scan, disconnect and stop WiFi (no event callbacks will fire)
    esp_wifi_scan_stop();
    esp_wifi_disconnect();
    esp_wifi_stop();

    if (station_netif_ != nullptr) {
        esp_netif_destroy_default_wifi(station_netif_);
        station_netif_ = nullptr;
    }
    
    // Reset was_connected_ flag to prevent stale state from affecting subsequent sessions
    was_connected_ = false;

    // Clear connected bit
    xEventGroupClearBits(event_group_, WIFI_EVENT_CONNECTED);
    
    // Set stopped event AFTER cleanup is complete to unblock WaitForConnected
    // This ensures no race condition with subsequent WiFi operations
    xEventGroupSetBits(event_group_, WIFI_EVENT_STOPPED);
}

void WifiStation::OnScanBegin(std::function<void()> on_scan_begin) {
    on_scan_begin_ = on_scan_begin;
}

void WifiStation::OnConnect(std::function<void(const std::string& ssid)> on_connect) {
    on_connect_ = on_connect;
}

void WifiStation::OnConnected(std::function<void(const std::string& ssid)> on_connected) {
    on_connected_ = on_connected;
}

void WifiStation::OnDisconnected(std::function<void(int reason)> on_disconnected) {
    on_disconnected_ = on_disconnected;
}

void WifiStation::Start() {
    // Note: esp_netif_init() and esp_wifi_init() should be called once before calling this method
    // WiFi driver is initialized by WifiManager::Initialize() and kept alive
    
    // Clear stopped event bit so WaitForConnected works properly
    // Clear scan done bit so Stop() can wait for scan to complete
    xEventGroupClearBits(event_group_, WIFI_EVENT_STOPPED | WIFI_EVENT_SCAN_DONE_BIT);
    
    // Create the default WiFi station interface
    station_netif_ = esp_netif_create_default_wifi_sta();
    if (!hostname_.empty()) {
        esp_err_t err = esp_netif_set_hostname(station_netif_, hostname_.c_str());
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Station DHCP hostname configured");
        } else {
            ESP_LOGW(TAG, "Failed to set station DHCP hostname: %s", esp_err_to_name(err));
        }
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiStation::WifiEventHandler,
                                                        this,
                                                        &instance_any_id_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiStation::IpEventHandler,
                                                        this,
                                                        &instance_got_ip_));
    // Setup the timer to scan WiFi.
    // skip_unhandled_events = false so the timer can wake the CPU from light
    // sleep on its own; otherwise an idle device that failed to connect would
    // never retry the scan, because esp_timer_get_next_alarm_for_wake_up
    // (components/esp_timer/src/esp_timer.c) excludes timers with this flag
    // from light-sleep wakeup sources.
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            esp_wifi_scan_start(nullptr, false);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "WiFiScanTimer",
        .skip_unhandled_events = false
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));

    /* WIFI_EVENT_STA_START can arrive immediately, so the rescan timer must
     * exist before the driver is started. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (max_tx_power_ != 0) {
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(max_tx_power_));
    }
}

bool WifiStation::WaitForConnected(int timeout_ms) {
    // Wait for either connected or stopped event
    auto bits = xEventGroupWaitBits(event_group_, WIFI_EVENT_CONNECTED | WIFI_EVENT_STOPPED, 
                                    pdFALSE, pdFALSE, timeout_ms / portTICK_PERIOD_MS);
    // Return true only if connected (not if stopped)
    return (bits & WIFI_EVENT_CONNECTED) != 0;
}

void WifiStation::HandleScanResult() {
    uint16_t ap_num = 0;
    if (esp_wifi_scan_get_ap_num(&ap_num) != ESP_OK) {
        esp_timer_start_once(timer_handle_, scan_current_interval_microseconds_);
        UpdateScanInterval();
        return;
    }
    wifi_ap_record_t *ap_records = nullptr;
    if (ap_num > 0) {
        ap_records = static_cast<wifi_ap_record_t *>(
            malloc(ap_num * sizeof(wifi_ap_record_t)));
        if (ap_records == nullptr) {
            ESP_LOGE(TAG, "Out of memory while collecting Wi-Fi scan results");
            esp_timer_start_once(timer_handle_, scan_current_interval_microseconds_);
            UpdateScanInterval();
            return;
        }
        if (esp_wifi_scan_get_ap_records(&ap_num, ap_records) != ESP_OK) {
            free(ap_records);
            esp_timer_start_once(timer_handle_, scan_current_interval_microseconds_);
            UpdateScanInterval();
            return;
        }
        // sort by rssi descending
        std::sort(ap_records, ap_records + ap_num,
                  [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
                      return a.rssi > b.rssi;
                  });
    }

    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    connect_queue_.clear();
    for (const auto& saved : ssid_list) {
        const wifi_ap_record_t *match = nullptr;
        for (uint16_t i = 0; i < ap_num; ++i) {
            if (strncmp(reinterpret_cast<const char *>(ap_records[i].ssid),
                        saved.ssid.c_str(), 33U) == 0) {
                match = &ap_records[i];
                break;
            }
        }
        WifiApRecord record = {
            .ssid = saved.ssid,
            .password = saved.password,
            .channel = 0,
            .authmode = WIFI_AUTH_OPEN,
            .bssid_valid = false,
            .bssid = {0},
        };
        if (match != nullptr) {
            ESP_LOGI(TAG, "Found a saved Wi-Fi network (details hidden)");
            record.channel = match->primary;
            record.authmode = match->authmode;
            record.bssid_valid = true;
            memcpy(record.bssid, match->bssid, sizeof(record.bssid));
        } else {
            /* A hidden network has no useful scan record. Let the station
             * driver actively scan all channels for the configured SSID. */
            ESP_LOGI(TAG,
                     "Trying an unseen saved Wi-Fi network (hidden fallback)");
        }
        connect_queue_.push_back(record);
    }
    free(ap_records);

    if (connect_queue_.empty()) {
        ESP_LOGI(TAG, "No AP found, next scan in %d seconds", scan_current_interval_microseconds_ / 1000 / 1000);
        esp_timer_start_once(timer_handle_, scan_current_interval_microseconds_);
        UpdateScanInterval();
        return;
    }

    StartConnect();
}

void WifiStation::StartConnect() {
    auto ap_record = connect_queue_.front();
    connect_queue_.erase(connect_queue_.begin());
    ssid_ = ap_record.ssid;
    password_ = ap_record.password;

    if (on_connect_) {
        on_connect_(ssid_);
    }

    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config));
    if (ap_record.ssid.empty() || ap_record.ssid.size() > 32U ||
        ap_record.password.size() > 64U) {
        ESP_LOGE(TAG, "Stored Wi-Fi credential has invalid bounds");
        if (!connect_queue_.empty()) StartConnect();
        return;
    }
    memcpy(wifi_config.sta.ssid, ap_record.ssid.data(),
           ap_record.ssid.size());
    memcpy(wifi_config.sta.password, ap_record.password.data(),
           ap_record.password.size());

    if (remember_bssid_ && ap_record.bssid_valid) {
        // Explicit opt-in: pin to this exact AP (BSSID + channel) for the fastest
        // reconnect. This intentionally disables roaming between same-SSID APs.
        wifi_config.sta.channel = ap_record.channel;
        memcpy(wifi_config.sta.bssid, ap_record.bssid, 6);
        wifi_config.sta.bssid_set = true;
    } else {
        // Default: do not lock a BSSID. Ask the driver to scan every channel and
        // connect to the same-SSID AP with the strongest signal, instead of the
        // first match found by the default fast scan (which may be a weaker
        // same-name hotspot). Because no BSSID is pinned, a later reconnect (e.g.
        // after moving to another room) will scan again and roam to whichever AP
        // is strongest at that time.
        wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        // Retry the strongest AP this many times before falling back to a weaker
        // same-SSID AP. Requires WIFI_ALL_CHANNEL_SCAN (set above). Without this
        // the driver gives up after a single auth failure and immediately moves on,
        // which is why we saw it connect to a -79 dBm AP even though a -73 dBm one
        // was available (the stronger AP rejected the first attempt).
        wifi_config.sta.failure_retry_cnt = failure_retry_cnt_;
    }
    wifi_config.sta.listen_interval = 10;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    reconnect_count_ = 0;
    ESP_ERROR_CHECK(esp_wifi_connect());
}

int8_t WifiStation::GetRssi() {
    // Check if connected first
    if (!IsConnected()) {
        return 0;  // Return 0 if not connected
    }
    
    // Get station info
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get AP info: %s", esp_err_to_name(err));
        return 0;
    }
    return ap_info.rssi;
}

uint8_t WifiStation::GetChannel() {
    // Check if connected first
    if (!IsConnected()) {
        return 0;  // Return 0 if not connected
    }
    
    // Get station info
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get AP info: %s", esp_err_to_name(err));
        return 0;
    }
    return ap_info.primary;
}

bool WifiStation::IsConnected() {
    return xEventGroupGetBits(event_group_) & WIFI_EVENT_CONNECTED;
}

void WifiStation::SetScanIntervalRange(int min_interval_seconds, int max_interval_seconds) {
    scan_min_interval_microseconds_ = min_interval_seconds * 1000 * 1000;
    scan_max_interval_microseconds_ = max_interval_seconds * 1000 * 1000;
    scan_current_interval_microseconds_ = scan_min_interval_microseconds_;
}


void WifiStation::SetPowerSaveLevel(WifiPowerSaveLevel level) {
    wifi_ps_type_t ps_type;
    switch (level) {
        case WifiPowerSaveLevel::LOW_POWER:
            ps_type = WIFI_PS_MAX_MODEM;  // Maximum power saving
            ESP_LOGI(TAG, "Setting WiFi power save level: LOW_POWER (MAX_MODEM)");
            break;
        case WifiPowerSaveLevel::BALANCED:
            ps_type = WIFI_PS_MIN_MODEM;  // Minimum power saving
            ESP_LOGI(TAG, "Setting WiFi power save level: BALANCED (MIN_MODEM)");
            break;
        case WifiPowerSaveLevel::PERFORMANCE:
        default:
            ps_type = WIFI_PS_NONE;       // No power saving
            ESP_LOGI(TAG, "Setting WiFi power save level: PERFORMANCE (NONE)");
            break;
    }
    ESP_ERROR_CHECK(esp_wifi_set_ps(ps_type));
}

void WifiStation::UpdateScanInterval() {
    // Apply exponential backoff: double the interval, up to max
    if (scan_current_interval_microseconds_ < scan_max_interval_microseconds_) {
        scan_current_interval_microseconds_ *= 2;
        if (scan_current_interval_microseconds_ > scan_max_interval_microseconds_) {
            scan_current_interval_microseconds_ = scan_max_interval_microseconds_;
        }
    }
}

// Static event handler functions
void WifiStation::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_scan_start(nullptr, false);
        if (this_->on_scan_begin_) {
            this_->on_scan_begin_();
        }
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(this_->event_group_, WIFI_EVENT_SCAN_DONE_BIT);
        this_->HandleScanResult();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(this_->event_group_, WIFI_EVENT_CONNECTED);
        
        // Notify disconnected callback only once when transitioning from connected to disconnected
        bool was_connected = this_->was_connected_;
        this_->was_connected_ = false;
        wifi_event_sta_disconnected_t* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        ESP_LOGI(TAG, "WiFi disconnected, reason: %d", event->reason);
        if (was_connected && this_->on_disconnected_) {
            this_->on_disconnected_(event->reason);
        }
        
        if (this_->reconnect_count_ < MAX_RECONNECT_COUNT) {
            esp_wifi_connect();
            this_->reconnect_count_++;
            ESP_LOGI(TAG, "Reconnecting (attempt %d / %d)",
                     this_->reconnect_count_, MAX_RECONNECT_COUNT);
            return;
        }

        if (!this_->connect_queue_.empty()) {
            this_->StartConnect();
            return;
        }
        
        ESP_LOGI(TAG, "No more AP to connect, next scan in %d seconds", 
                 this_->scan_current_interval_microseconds_ / 1000 / 1000);
        esp_timer_start_once(this_->timer_handle_, this_->scan_current_interval_microseconds_);
        this_->UpdateScanInterval();
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
    }
}

void WifiStation::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);

    char ip_address[16];
    esp_ip4addr_ntoa(&event->ip_info.ip, ip_address, sizeof(ip_address));
    this_->ip_address_ = ip_address;
    ESP_LOGI(TAG, "Station obtained an IP address");
    
    xEventGroupSetBits(this_->event_group_, WIFI_EVENT_CONNECTED);
    this_->was_connected_ = true;  // Mark as connected for disconnect notification
    if (this_->on_connected_) {
        this_->on_connected_(this_->ssid_);
    }
    this_->connect_queue_.clear();
    this_->reconnect_count_ = 0;
    
    // Reset scan interval to minimum for fast reconnect if disconnected later
    this_->scan_current_interval_microseconds_ = this_->scan_min_interval_microseconds_;
}
