#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
typedef struct { const char *server; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(name) {.server = (name)}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *);
esp_err_t esp_netif_sntp_sync_wait(TickType_t);
void esp_netif_sntp_deinit(void);
