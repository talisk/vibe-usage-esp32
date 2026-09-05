#ifndef SSID_MANAGER_H
#define SSID_MANAGER_H

#include <string>
#include <vector>

#include <esp_err.h>

struct SsidItem {
    std::string ssid;
    std::string password;
};

class SsidManager {
public:
    static SsidManager& GetInstance() {
        static SsidManager instance;
        return instance;
    }

    esp_err_t AddSsid(const std::string& ssid, const std::string& password);
    esp_err_t RemoveSsid(int index);
    esp_err_t SetDefaultSsid(int index);
    esp_err_t Clear();
    const std::vector<SsidItem>& GetSsidList() const { return ssid_list_; }

private:
    SsidManager();
    ~SsidManager();

    void LoadFromNvs();
    esp_err_t SaveToNvs();

    std::vector<SsidItem> ssid_list_;
};

#endif // SSID_MANAGER_H
