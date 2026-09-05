idf_component_register(
    SRCS "device_config.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash vibe_usage esp_hw_support vibe_ui
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
