#ifndef DEVICE_CONFIG_H_
#define DEVICE_CONFIG_H_

#include <stdint.h>

#include "esp_err.h"
#include "vibe_usage.h"
#include "vibe_i18n.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t schema_version;
    uint32_t config_revision;
    uint32_t refresh_seconds;
    vibe_timezone_t timezone;
    vibe_language_t language;
    char device_id[5];
} device_config_t;

esp_err_t device_config_load_or_create(uint32_t default_refresh_seconds,
                                       device_config_t *config);
esp_err_t device_config_save(device_config_t *config);
esp_err_t device_config_save_language(vibe_language_t language);

#ifdef __cplusplus
}
#endif

#endif  // DEVICE_CONFIG_H_
