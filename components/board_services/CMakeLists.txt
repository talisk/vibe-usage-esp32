if(IDF_TARGET STREQUAL "esp32c3")
    set(board_dependency bsp)
    set(nfc_source "board_nfc_passport.c")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(board_dependency zectrix_board)
    set(nfc_source "board_nfc_note.cc")
else()
    message(FATAL_ERROR "board_services supports Passport C3 and NOTE4 S3 only")
endif()
idf_component_register(
    SRCS "board_audio.cc" "board_ndef.c" ${nfc_source}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_timer
    PRIV_REQUIRES ${board_dependency} esp_codec_dev
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
