idf_component_register(
    SRCS "wifi_adapter.cc"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi_connect esp_timer json
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
