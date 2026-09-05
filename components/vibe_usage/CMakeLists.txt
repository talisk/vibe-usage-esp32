idf_component_register(
    SRCS
        "src/vibe_usage.c"
        "src/vibe_date.c"
        "src/vibe_aggregate.c"
        "src/vibe_cache.c"
        "src/vibe_parser.c"
        "src/vibe_state.c"
        "src/vibe_http_policy.c"
        "src/vibe_json_safety.c"
        "src/vibe_device_flow_policy.c"
        "src/vibe_http.c"
        "src/vibe_store.c"
        "src/vibe_client.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_http_client esp-tls nvs_flash json esp_timer esp_app_format
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror
    -fstack-usage)
