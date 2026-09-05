idf_component_register(
    SRCS "app_main.cc" "vibe_note4_ui.cc" "epd_refresh_policy.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_controller
        device_config
        esp_app_format
        espressif__qrcode
        zectrix_board
        zectrix_canvas
        zectrix_epd
        nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror
    -fstack-usage)
