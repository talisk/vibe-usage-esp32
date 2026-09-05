idf_component_register(
    SRCS "app_main.c" "vibe_passport_ui.c"
        "../../../components/vibe_ui/generated/vibe_font_12.c"
        "../../../components/vibe_ui/generated/vibe_font_14.c"
        "../../../components/vibe_ui/generated/vibe_font_20.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_controller
        bsp
        device_config
        esp_app_format
        esp_timer
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror
    -fstack-usage)
