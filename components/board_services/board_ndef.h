#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Pure, bounded NDEF encoders; no device access or logging. */
bool board_ndef_wifi(const char *ssid, const char *password, const char *url,
                     uint8_t *out, size_t capacity, size_t *written);
bool board_ndef_uri(const char *url, uint8_t *out, size_t capacity, size_t *written);
bool board_ndef_type2(const uint8_t *message, size_t length, uint8_t *out,
                      size_t capacity, size_t *written);
#ifdef __cplusplus
}
#endif
