#include "board_services.h"

/* NTAG213 is RF-only and physically independent from the ESP32-C3.
 * See the upstream Passport hardware development guide, section 3.2. */
esp_err_t board_nfc_set_wifi(const char *ssid, const char *password, const char *url) {
    (void)ssid; (void)password; (void)url;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t board_nfc_set_url(const char *url) {
    (void)url;
    return ESP_ERR_NOT_SUPPORTED;
}
void board_nfc_stop(void) {}
esp_err_t board_nfc_last_error(void) { return ESP_ERR_NOT_SUPPORTED; }
