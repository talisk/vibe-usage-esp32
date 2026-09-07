#include "board_services.h"

#include <algorithm>
#include <climits>
#include "esp_attr.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C3
extern "C" {
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "bsp_button.h"
}
#else
#include "i2c_bus_lock.h"
#include "zectrix_board_config.h"
extern "C" void BoardI2cForcePowerOn();
#endif

namespace {
constexpr char kTag[] = "board_audio";
constexpr size_t kDmaSamples = 160; // 20 ms, never a long recording buffer.
const audio_codec_ctrl_if_t *ctrl;
const audio_codec_data_if_t *data;
const audio_codec_if_t *codec;
const audio_codec_gpio_if_t *gpio_if;
esp_codec_dev_handle_t dev;
i2s_chan_handle_t tx, rx;
bool opened, capturing, channels_need_enable, recovery_needed;
uint8_t alert_volume = 50;
DRAM_ATTR portMUX_TYPE capture_mux = portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR bool capture_overflow = false;

bool IRAM_ATTR CaptureOverflow(i2s_chan_handle_t, i2s_event_data_t *, void *) {
    // Never treat a gapped voice stream as a valid command. ISR only signals;
    // the serialized controller stops capture and cancels its HTTP request.
    portENTER_CRITICAL_ISR(&capture_mux);
    capture_overflow = true;
    portEXIT_CRITICAL_ISR(&capture_mux);
    return false;
}

bool CaptureOverflowed() {
    portENTER_CRITICAL(&capture_mux);
    const bool overflow = capture_overflow;
    portEXIT_CRITICAL(&capture_mux);
    return overflow;
}

void ClearCaptureOverflow() {
    portENTER_CRITICAL(&capture_mux);
    capture_overflow = false;
    portEXIT_CRITICAL(&capture_mux);
}

class BusGuard {
public:
#if CONFIG_IDF_TARGET_ESP32S3
    BusGuard() : lock_("board_audio", pdMS_TO_TICKS(1000)) {}
    esp_err_t status() const { return lock_.status(); }
private:
    ScopedI2cBusLock lock_;
#else
    esp_err_t status() const { return ESP_OK; }
#endif
};

void Speaker(bool enabled) {
#if CONFIG_IDF_TARGET_ESP32S3
    gpio_hold_dis(ZECTRIX_AUDIO_PA);
    gpio_set_level(ZECTRIX_AUDIO_PA, enabled ? 1 : 0);
    gpio_hold_en(ZECTRIX_AUDIO_PA);
#else
    (void)enabled; // Passport PA is always powered; codec mute remains required.
#endif
}

void Dispose() {
    Speaker(false);
    if (dev) {
        if (opened) esp_codec_dev_close(dev);
        esp_codec_dev_delete(dev);
    }
    dev = nullptr; opened = false; capturing = false; channels_need_enable = false;
    recovery_needed = false;
    if (codec) audio_codec_delete_codec_if(codec);
    if (ctrl) audio_codec_delete_ctrl_if(ctrl);
    if (gpio_if) audio_codec_delete_gpio_if(gpio_if);
    if (data) audio_codec_delete_data_if(data);
    codec = nullptr; ctrl = nullptr; gpio_if = nullptr; data = nullptr;
    if (tx) { i2s_channel_disable(tx); i2s_del_channel(tx); }
    if (rx) { i2s_channel_disable(rx); i2s_del_channel(rx); }
    tx = nullptr; rx = nullptr;
}

esp_err_t Initialize() {
    if (dev) return ESP_OK;
    i2c_master_bus_handle_t bus = nullptr;
#if CONFIG_IDF_TARGET_ESP32C3
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) return err;
    bus = bsp_i2c_bus();
    constexpr gpio_num_t mclk = (gpio_num_t)BSP_I2S_MCLK;
    constexpr gpio_num_t bclk = (gpio_num_t)BSP_I2S_BCLK;
    constexpr gpio_num_t ws = (gpio_num_t)BSP_I2S_WS;
    constexpr gpio_num_t dout = (gpio_num_t)BSP_I2S_DOUT;
    constexpr gpio_num_t din = (gpio_num_t)BSP_I2S_DIN;
#else
    // The board already owns I2C0 for RTC. Never allocate another I2C or ADC.
    esp_err_t err = i2c_master_get_bus_handle(I2C_NUM_0, &bus);
    if (err != ESP_OK) return err;
    BoardI2cForcePowerOn();
    gpio_config_t pa = {};
    pa.pin_bit_mask = 1ULL << ZECTRIX_AUDIO_PA;
    pa.mode = GPIO_MODE_OUTPUT;
    err = gpio_config(&pa);
    if (err != ESP_OK) return err;
    Speaker(false);
    constexpr gpio_num_t mclk = ZECTRIX_AUDIO_MCLK;
    constexpr gpio_num_t bclk = ZECTRIX_AUDIO_BCLK;
    constexpr gpio_num_t ws = ZECTRIX_AUDIO_WS;
    constexpr gpio_num_t dout = ZECTRIX_AUDIO_DOUT;
    constexpr gpio_num_t din = ZECTRIX_AUDIO_DIN;
#endif
    BusGuard guard;
    if (guard.status() != ESP_OK) return guard.status();
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 4;
    channel.dma_frame_num = kDmaSamples;
    channel.auto_clear_after_cb = true;
    err = i2s_new_channel(&channel, &tx, &rx);
    if (err != ESP_OK) return err;
    i2s_std_config_t standard = {};
    standard.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BOARD_AUDIO_SAMPLE_RATE);
    standard.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                          I2S_SLOT_MODE_STEREO);
    standard.gpio_cfg.mclk = mclk; standard.gpio_cfg.bclk = bclk;
    standard.gpio_cfg.ws = ws; standard.gpio_cfg.dout = dout;
    standard.gpio_cfg.din = din;
    if ((err = i2s_channel_init_std_mode(tx, &standard)) != ESP_OK ||
        (err = i2s_channel_init_std_mode(rx, &standard)) != ESP_OK) {
        Dispose(); return err;
    }
    i2s_event_callbacks_t callbacks = {};
    callbacks.on_recv_q_ovf = CaptureOverflow;
    if ((err = i2s_channel_register_event_callback(rx, &callbacks, nullptr)) != ESP_OK) {
        Dispose(); return err;
    }
    // codec_dev reconfigures both channels to mono on open.
    if ((err = i2s_channel_enable(tx)) != ESP_OK ||
        (err = i2s_channel_enable(rx)) != ESP_OK) {
        Dispose(); return err;
    }
    audio_codec_i2c_cfg_t control = {};
    control.port = I2C_NUM_0; control.addr = 0x18 << 1; control.bus_handle = bus;
    ctrl = audio_codec_new_i2c_ctrl(&control);
    audio_codec_i2s_cfg_t interface = {};
    interface.port = I2S_NUM_0; interface.tx_handle = tx; interface.rx_handle = rx;
    data = audio_codec_new_i2s_data(&interface);
    gpio_if = audio_codec_new_gpio();
    if (!ctrl || !data || !gpio_if) { Dispose(); return ESP_ERR_NO_MEM; }
    es8311_codec_cfg_t configuration = {};
    configuration.ctrl_if = ctrl; configuration.gpio_if = gpio_if;
    configuration.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    configuration.pa_pin = -1; // PA is only enabled for the local chime.
    configuration.use_mclk = true;
    configuration.no_dac_ref = true; // Avoid a DAC reference channel instead of mic.
    configuration.hw_gain.pa_voltage = 5.0f;
    configuration.hw_gain.codec_dac_voltage = 3.3f;
    codec = es8311_codec_new(&configuration);
    if (!codec) { Dispose(); return ESP_FAIL; }
    esp_codec_dev_cfg_t device = {};
    device.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT;
    device.codec_if = codec; device.data_if = data;
    dev = esp_codec_dev_new(&device);
    if (!dev) { Dispose(); return ESP_ERR_NO_MEM; }
    return ESP_OK;
}

esp_err_t Open() {
    if (recovery_needed) {
        BusGuard recovery;
        if (recovery.status() != ESP_OK) return recovery.status();
        Dispose();
    }
    esp_err_t err = Initialize();
    if (err != ESP_OK || opened) return err;
    BusGuard guard;
    if (guard.status() != ESP_OK) return guard.status();
    // codec_dev 1.6.2 reconfiguration first disables RUNNING channels. Its
    // close leaves READY channels; match the verified upstream BSP workaround.
    if (channels_need_enable) {
        err = i2s_channel_enable(tx);
        if (err == ESP_OK) err = i2s_channel_enable(rx);
        if (err != ESP_OK) { Dispose(); return err; }
        channels_need_enable = false;
    }
    esp_codec_dev_sample_info_t format = {};
    format.bits_per_sample = 16;
    format.channel = 1;
    format.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0);
    format.sample_rate = BOARD_AUDIO_SAMPLE_RATE;
    if (esp_codec_dev_open(dev, &format) != ESP_CODEC_DEV_OK) { Dispose(); return ESP_FAIL; }
    opened = true;
    if (esp_codec_dev_set_in_gain(dev, 30.0f) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(dev, alert_volume) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_mute(dev, true) != ESP_CODEC_DEV_OK) {
        Dispose(); return ESP_FAIL;
    }
    return ESP_OK;
}

void Close() {
    Speaker(false);
    if (!opened) return;
    BusGuard guard;
    if (guard.status() != ESP_OK) {
        // DMA stop does not need I2C. A contended control bus must never leave
        // the microphone recording after the user released OK.
        i2s_channel_disable(rx);
        i2s_channel_disable(tx);
        recovery_needed = true;
        ESP_LOGE(kTag, "codec close lock failed; audio DMA stopped"); return;
    }
    esp_codec_dev_set_out_mute(dev, true);
    if (esp_codec_dev_close(dev) == ESP_CODEC_DEV_OK) {
        opened = false;
        channels_need_enable = true;
    } else {
        ESP_LOGE(kTag, "codec close failed");
        // Ensure a failed codec close does not keep the microphone DMA running.
        Dispose();
    }
}

bool ReminderSounding(unsigned frame) {
    /* 20 ms frames: 3 x (140 ms on, 80 ms gap), then exactly 1 s silent. */
    constexpr unsigned kGroupFrames = 29;
    constexpr unsigned kSecondGroup = kGroupFrames + 50;
    if (frame >= kSecondGroup) frame -= kSecondGroup;
    else if (frame >= kGroupFrames) return false;
    return frame < 7 || (frame >= 11 && frame < 18) ||
           (frame >= 22 && frame < 29);
}

esp_err_t PlayChime(bool reminder) {
    if (capturing) return ESP_ERR_INVALID_STATE;
    if (alert_volume == 0) return ESP_OK;
    esp_err_t err = Open();
    if (err != ESP_OK) return err;
    {
        BusGuard guard;
        if (guard.status() != ESP_OK) { Close(); return guard.status(); }
        if (esp_codec_dev_set_out_vol(dev, alert_volume) != ESP_CODEC_DEV_OK ||
            esp_codec_dev_set_out_mute(dev, false) != ESP_CODEC_DEV_OK) {
            Close(); return ESP_FAIL;
        }
    }
    Speaker(true);
    static constexpr int16_t sine[16] = {
        0, 1531, 2828, 3696, 4000, 3696, 2828, 1531,
        0, -1531, -2828, -3696, -4000, -3696, -2828, -1531};
    int16_t block[kDmaSamples];
    const unsigned frames = reminder ? 112 : 35;
    for (unsigned frame = 0; frame < frames && err == ESP_OK; ++frame) {
        const bool sounding = reminder ? ReminderSounding(frame)
                                        : frame < 12 || (frame >= 17 && frame < 29);
        for (size_t sample = 0; sample < kDmaSamples; ++sample) {
            const unsigned position = frame * kDmaSamples + sample;
            const unsigned phase = position * (reminder || frame >= 17 ? 2 : 1);
            block[sample] = sounding ? sine[phase % 16] : 0;
        }
        size_t written = 0;
        err = i2s_channel_write(tx, block, sizeof(block), &written, 150);
        if (err == ESP_OK && written != sizeof(block)) err = ESP_FAIL;
    }
    /* TX queues DMA; the reminder includes four trailing silent frames. */
    vTaskDelay(pdMS_TO_TICKS(80));
    Close();
    return err;
}
} // namespace

extern "C" bool board_ok_is_pressed(void) {
#if CONFIG_IDF_TARGET_ESP32C3
    return bsp_button_is_pressed(BSP_BTN_OK);
#else
    return gpio_get_level(ZECTRIX_BUTTON_OK) == 0;
#endif
}

extern "C" esp_err_t board_audio_start_capture(void) {
    if (capturing) return ESP_ERR_INVALID_STATE;
    esp_err_t err = Open();
    if (err != ESP_OK) return err;
    Speaker(false);
    // Discard queued data from the preceding session plus codec startup transient.
    int16_t discard[kDmaSamples];
    size_t bytes = 0;
    for (unsigned i = 0; i < 4; ++i)
        i2s_channel_read(rx, discard, sizeof(discard), &bytes, 0);
    for (unsigned i = 0; i < 2; ++i) {
        err = i2s_channel_read(rx, discard, sizeof(discard), &bytes, 100);
        if (err != ESP_OK) { Close(); return err; }
    }
    // Ignore old RX events generated during the chime/startup discard phase.
    ClearCaptureOverflow();
    capturing = true;
    return ESP_OK;
}

extern "C" esp_err_t board_audio_read(int16_t *samples, size_t capacity,
                                      size_t *read_samples, uint32_t timeout_ms) {
    if (read_samples) *read_samples = 0;
    if (!samples || !read_samples || !capacity || capacity > SIZE_MAX / sizeof(int16_t))
        return ESP_ERR_INVALID_ARG;
    if (!capturing || !opened) return ESP_ERR_INVALID_STATE;
    if (CaptureOverflowed()) return ESP_ERR_INVALID_RESPONSE;
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(rx, samples, capacity * sizeof(int16_t),
                                    &bytes, timeout_ms);
    if (CaptureOverflowed()) return ESP_ERR_INVALID_RESPONSE;
    *read_samples = bytes / sizeof(int16_t);
    return err;
}

extern "C" void board_audio_stop_capture(void) {
    capturing = false;
    Close();
    ClearCaptureOverflow();
}

extern "C" esp_err_t board_audio_set_alert_volume(uint8_t volume) {
    if (volume > 100) return ESP_ERR_INVALID_ARG;
    alert_volume = volume;
    return ESP_OK;
}

extern "C" esp_err_t board_audio_beep(void) {
    return PlayChime(false);
}

extern "C" esp_err_t board_audio_reminder(void) {
    return PlayChime(true);
}
