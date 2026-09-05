idf_component_register(
    SRCS "app_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES
        device_config
        vibe_usage
        wifi_adapter
        esp_netif
        esp_timer
        esp_system
        esp_app_format
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror
    -fstack-usage)
