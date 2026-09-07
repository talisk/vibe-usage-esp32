#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All calls belong to the controller task. No audio is stored in flash.
 * Samples are 8 kHz, mono, signed PCM16; read may return a short block.
 * On timeout, read_samples may still contain valid samples. */
#define BOARD_AUDIO_SAMPLE_RATE 8000
/* Existing driver/GPIO level; no new ADC allocation. Combine with the logical
 * hold flag to reject a START queued after the physical release. */
bool board_ok_is_pressed(void);
esp_err_t board_audio_start_capture(void);
esp_err_t board_audio_read(int16_t *samples, size_t capacity,
                          size_t *read_samples, uint32_t timeout_ms);
void board_audio_stop_capture(void);
/* Applies to local confirmation/reminder chimes. Zero is muted. */
esp_err_t board_audio_set_alert_volume(uint8_t volume);
/* A bounded local chime. Returns INVALID_STATE during capture. */
esp_err_t board_audio_beep(void);
/* TODO due alert: three short beeps, one second silence, then three beeps. */
esp_err_t board_audio_reminder(void);

/* NOTE4: Wi-Fi WSC credential plus URI NDEF, then URI-only portal stage.
 * Passport: independent passive NTAG213 has no MCU connection; returns
 * ESP_ERR_NOT_SUPPORTED. Never give this API an LLM key or session token. */
esp_err_t board_nfc_set_wifi(const char *ssid, const char *password,
                            const char *url);
esp_err_t board_nfc_set_url(const char *url);
/* Also call once at controller startup to remove power-loss leftovers.
 * Writes a safe URI and clears the rest of the NDEF user area before power
 * off. Power off alone does not prevent RF reads of persistent NFC data. */
void board_nfc_stop(void);
esp_err_t board_nfc_last_error(void);

#ifdef __cplusplus
}
#endif
