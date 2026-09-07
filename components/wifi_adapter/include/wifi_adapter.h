#ifndef WIFI_ADAPTER_H_
#define WIFI_ADAPTER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_ADAPTER_SCANNING = 0,
    WIFI_ADAPTER_CONNECTING,
    WIFI_ADAPTER_GOT_IP,
    WIFI_ADAPTER_DISCONNECTED,
    WIFI_ADAPTER_PORTAL_ENTER,
    WIFI_ADAPTER_PROVISIONING_TIMEOUT,
    WIFI_ADAPTER_PORTAL_EXIT,
} wifi_adapter_event_t;

typedef void (*wifi_adapter_event_cb_t)(wifi_adapter_event_t event,
                                        void *context);

typedef struct {
    const char *product_prefix;
    const char *device_id;
    const char *station_hostname;
    const char *product_name;
    const char *firmware_version;
    const char *repository_url;
    const char *author_url;
    const char *language;
    uint32_t provisioning_timeout_seconds;
    wifi_adapter_event_cb_t event_callback;
    void *event_context;
} wifi_adapter_config_t;

esp_err_t wifi_adapter_init(const wifi_adapter_config_t *config);
void wifi_adapter_set_language(const char *language);
bool wifi_adapter_has_credentials(void);
size_t wifi_adapter_credential_count(void);
esp_err_t wifi_adapter_start_station(void);
void wifi_adapter_stop_station(void);
esp_err_t wifi_adapter_start_provisioning(void);
void wifi_adapter_stop_provisioning(void);
bool wifi_adapter_is_connected(void);
bool wifi_adapter_is_provisioning(void);
const char *wifi_adapter_ap_ssid(void);
const char *wifi_adapter_portal_url(void);
/* Copies the active station IPv4 address; never returns the setup AP address. */
esp_err_t wifi_adapter_station_ip(char *buffer, size_t capacity);
esp_err_t wifi_adapter_remove_credential(size_t index);
esp_err_t wifi_adapter_clear_credentials(void);

#ifdef __cplusplus
}
#endif

#endif  // WIFI_ADAPTER_H_
