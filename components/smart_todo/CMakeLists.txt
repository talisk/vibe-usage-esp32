idf_component_register(
    SRCS "smart_todo.c" "smart_todo_store.c" "smart_todo_llm.c"
    INCLUDE_DIRS "include"
    REQUIRES json nvs_flash esp_http_client esp_partition mbedtls esp_timer board_services llm_portal
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fstack-usage)
