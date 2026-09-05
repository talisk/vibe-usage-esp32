# Zectrix board hardware map

The standalone board adapter is in `components/zectrix_board`. The defaults
target the current Zectrix 4.2-inch ESP32-S3 e-paper board.

## GPIO map

| Function | GPIO / address | Notes |
| --- | --- | --- |
| OK button | GPIO0 | Active low |
| UP button | GPIO39 | Active low |
| DOWN / power button | GPIO18 | Active low; 3-second shutdown hold |
| Battery power latch | GPIO17 | High keeps battery rail on |
| Power LED | GPIO3 | Active low |
| Audio rail | GPIO42 | Active high |
| Audio MCLK / BCLK / WS | GPIO14 / 15 / 38 | ES8311 |
| Audio data out / in | GPIO45 / 16 | ESP32 perspective |
| Speaker PA | GPIO46 | Amplifier enable |
| I2C SDA / SCL | GPIO47 / 48 | RTC, NFC and audio control |
| RTC interrupt | GPIO5 | PCF8563 at address `0x51` |
| NFC power / field detect | GPIO21 / GPIO7 | NFC at address `0x55` |
| Charge detect / full | GPIO2 / GPIO1 | Charger status inputs |

The EPD GPIO and SPI defaults are public through
`zectrix_epd_get_default_config()`. Applications may override any value in the
returned `zectrix_epd_config_t` before creating the driver.

## Power behavior

At boot, the product firmware asserts the battery latch before NVS or
peripheral initialization. The display has a separate controlled rail and
remains off until a refresh. The locked upstream audio/NFC source is retained
for provenance, but P0 does not compile it; NFC power is held low and no UID is
read or logged.

Battery voltage is read through the board ADC path and displayed as both
millivolts and an estimated percentage. The charging test combines charger
status pins with the battery measurement to reject a false pass when no
battery is fitted.

## Porting to another revision

1. Update `components/zectrix_board/include/zectrix_board_config.h`.
2. Override the EPD configuration in `main/app_main.cc` if its SPI wiring
   changed.
3. Confirm flash size, PSRAM mode and partition layout in `sdkconfig.defaults`.
4. Re-run every item in `docs/TEST_CRITERIA.md` on real hardware.
