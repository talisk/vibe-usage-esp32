#include "board_services.h"
#include "board_ndef.h"

#include <cstring>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus_lock.h"
#include "zectrix_board_config.h"

extern "C" void BoardI2cForcePowerOn();

namespace {
constexpr char kTag[] = "board_nfc";
constexpr char kSafeUrl[] = "http://192.168.4.1/";
constexpr size_t kBlock = 16;
constexpr size_t kCapacity = 55 * kBlock; // Existing NOTE4 map: blocks 01..37.
i2c_master_dev_handle_t device;
esp_err_t last_error = ESP_ERR_INVALID_STATE;

void SecureClear(void *data, size_t length) {
    volatile uint8_t *bytes = static_cast<volatile uint8_t *>(data);
    while (length--) *bytes++ = 0;
}
void Power(bool enabled) {
    gpio_hold_dis(ZECTRIX_NFC_POWER);
    gpio_set_level(ZECTRIX_NFC_POWER, enabled ? 1 : 0);
    gpio_hold_en(ZECTRIX_NFC_POWER);
}
bool HasField() {
    return gpio_get_level(ZECTRIX_NFC_FD) == ZECTRIX_NFC_FD_ACTIVE_LEVEL;
}
esp_err_t Initialize() {
    if (!device) {
        i2c_master_bus_handle_t bus = nullptr;
        esp_err_t err = i2c_master_get_bus_handle(I2C_NUM_0, &bus);
        if (err != ESP_OK) return err;
        gpio_config_t power = {};
        power.pin_bit_mask = 1ULL << ZECTRIX_NFC_POWER;
        power.mode = GPIO_MODE_OUTPUT;
        gpio_hold_dis(ZECTRIX_NFC_POWER);
        if ((err = gpio_config(&power)) != ESP_OK) return err;
        gpio_config_t field = {};
        field.pin_bit_mask = 1ULL << ZECTRIX_NFC_FD;
        field.mode = GPIO_MODE_INPUT;
        field.pull_up_en = GPIO_PULLUP_ENABLE;
        if ((err = gpio_config(&field)) != ESP_OK) return err;
        i2c_device_config_t config = {};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = ZECTRIX_NFC_ADDR;
        config.scl_speed_hz = 100000;
        if ((err = i2c_master_bus_add_device(bus, &config, &device)) != ESP_OK) return err;
    }
    BoardI2cForcePowerOn();
    Power(true);
    vTaskDelay(pdMS_TO_TICKS(10));
    return HasField() ? ESP_ERR_TIMEOUT : ESP_OK;
}
esp_err_t ReadBlock(uint8_t address, uint8_t *data) {
    if (address < 1 || address > 0x37) return ESP_ERR_INVALID_ARG;
    // Use the STOP and read delay required by the locked NOTE4 upstream driver.
    esp_err_t err = i2c_master_transmit(device, &address, 1, 100);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    return i2c_master_receive(device, data, kBlock, 100);
}
esp_err_t WriteBlock(uint8_t address, const uint8_t *data) {
    if (address < 1 || address > 0x37) return ESP_ERR_INVALID_ARG;
    if (HasField()) return ESP_ERR_TIMEOUT;
    uint8_t transfer[kBlock + 1];
    transfer[0] = address;
    memcpy(transfer + 1, data, kBlock);
    esp_err_t err = i2c_master_transmit(device, transfer, sizeof(transfer), 100);
    SecureClear(transfer, sizeof(transfer));
    vTaskDelay(pdMS_TO_TICKS(10)); // EEPROM write completion, no busy-spin.
    return err;
}
esp_err_t WriteChecked(uint8_t address, const uint8_t *data) {
    uint8_t actual[kBlock];
    esp_err_t err = ReadBlock(address, actual);
    if (err == ESP_OK && memcmp(actual, data, kBlock)) {
        err = WriteBlock(address, data);
        if (err == ESP_OK) err = ReadBlock(address, actual);
        if (err == ESP_OK && memcmp(actual, data, kBlock)) err = ESP_ERR_INVALID_RESPONSE;
    }
    SecureClear(actual, sizeof(actual));
    return err;
}
esp_err_t Publish(const uint8_t *message, size_t length) {
    // Zero-fill ALL user bytes to remove older, longer credential messages.
    uint8_t image[kCapacity] = {};
    size_t tlv_length = 0;
    if (!board_ndef_type2(message, length, image, sizeof(image), &tlv_length))
        return ESP_ERR_INVALID_SIZE;
    ScopedI2cBusLock guard("board_nfc", pdMS_TO_TICKS(1000));
    esp_err_t err = guard.status();
    if (err == ESP_OK) err = Initialize();
    if (err == ESP_OK) {
        // An empty TLV hides the message during multi-block updates. Commit the
        // first block last. Never write UID/CC/config/lock/register blocks.
        uint8_t empty[kBlock] = {3, 0, 0xfe};
        err = WriteChecked(1, empty);
        for (uint8_t block = 2; block <= 0x37 && err == ESP_OK; ++block)
            err = WriteChecked(block, image + (block - 1) * kBlock);
        if (err == ESP_OK) err = WriteChecked(1, image);
    }
    SecureClear(image, sizeof(image));
    return err;
}
} // namespace

extern "C" esp_err_t board_nfc_set_wifi(const char *ssid, const char *password,
                                        const char *url) {
    uint8_t message[400];
    size_t length = 0;
    last_error = board_ndef_wifi(ssid, password, url, message, sizeof(message), &length)
        ? Publish(message, length) : ESP_ERR_INVALID_ARG;
    SecureClear(message, sizeof(message));
    return last_error;
}
extern "C" esp_err_t board_nfc_set_url(const char *url) {
    uint8_t message[200];
    size_t length = 0;
    last_error = board_ndef_uri(url, message, sizeof(message), &length)
        ? Publish(message, length) : ESP_ERR_INVALID_ARG;
    SecureClear(message, sizeof(message));
    return last_error;
}
extern "C" void board_nfc_stop(void) {
    // RF still works with GPIO21 low. Cleanup must be a verified EEPROM write.
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (board_nfc_set_url(kSafeUrl) == ESP_OK) break;
        if (last_error != ESP_ERR_TIMEOUT) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (last_error != ESP_OK) {
        ESP_LOGE(kTag, "NFC cleanup failed: %s", esp_err_to_name(last_error));
        // Preserve an active phone RF session and permit a later cleanup
        // retry. Power cycling would not erase EEPROM and proves nothing.
        return;
    }
    if (device) Power(false);
}
extern "C" esp_err_t board_nfc_last_error(void) { return last_error; }
