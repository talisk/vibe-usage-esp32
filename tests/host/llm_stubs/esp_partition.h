#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef uint8_t esp_partition_type_t;
typedef uint8_t esp_partition_subtype_t;
typedef struct esp_partition_t {
    size_t size;
    const char *label;
} esp_partition_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 0xff
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
    esp_partition_subtype_t subtype, const char *label);
esp_err_t esp_partition_erase_range(const esp_partition_t *partition,
    size_t offset, size_t size);
esp_err_t esp_partition_write(const esp_partition_t *partition,
    size_t offset, const void *source, size_t size);
esp_err_t esp_partition_read(const esp_partition_t *partition,
    size_t offset, void *destination, size_t size);
