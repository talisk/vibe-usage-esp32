idf_component_register(
    SRCS "llm_endpoint.c" "llm_settings.c" "llm_portal.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server nvs_flash esp_hw_support esp_timer wifi_adapter json
    EMBED_TXTFILES "portal.html"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
