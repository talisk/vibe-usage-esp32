cmake_minimum_required(VERSION 3.16)
project(passport_layout_test C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(VIBE_UI_TARGET "all" CACHE STRING "UI tests: all, passport or note4")
if(NOT VIBE_UI_TARGET MATCHES "^(all|passport|note4)$")
    message(FATAL_ERROR "Invalid VIBE_UI_TARGET: ${VIBE_UI_TARGET}")
endif()
if(VIBE_UI_TARGET STREQUAL "all" OR VIBE_UI_TARGET STREQUAL "passport")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO_ROOT}/firmware/ai-passport/managed_components/lvgl__lvgl" lvgl)
add_executable(test_passport_layout test_passport_layout.c)
target_sources(test_passport_layout PRIVATE
    "${REPO_ROOT}/components/vibe_ui/generated/vibe_font_12.c"
    "${REPO_ROOT}/components/vibe_ui/generated/vibe_font_14.c"
    "${REPO_ROOT}/components/vibe_ui/generated/vibe_font_20.c"
    "${REPO_ROOT}/components/vibe_ui/generated/vibe_todo_font_12.c"
    "${REPO_ROOT}/components/vibe_ui/generated/vibe_glyphs.c"
    "${REPO_ROOT}/components/vibe_ui/generated/vibe_messages.c"
    "${REPO_ROOT}/components/vibe_ui/vibe_i18n.c")
target_include_directories(test_passport_layout PRIVATE "${REPO_ROOT}/components/vibe_ui/include")
target_include_directories(test_passport_layout PRIVATE "${REPO_ROOT}/firmware/ai-passport/main")
target_include_directories(test_passport_layout PRIVATE "${REPO_ROOT}/components/app_controller/include")
target_link_libraries(test_passport_layout PRIVATE lvgl m)
target_compile_options(test_passport_layout PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()

if(VIBE_UI_TARGET STREQUAL "all" OR VIBE_UI_TARGET STREQUAL "note4")
set(NOTE_QR "${REPO_ROOT}/firmware/zectrix/managed_components/espressif__qrcode")
add_executable(test_note_qr test_note_qr.cc
    "${NOTE_QR}/qrcodegen.c" "${NOTE_QR}/esp_qrcode_wrapper.c"
    "${REPO_ROOT}/firmware/zectrix/components/zectrix_canvas/zectrix_canvas.cc"
    "${REPO_ROOT}/components/vibe_ui/generated/vibe_todo_glyphs.c"
    "${REPO_ROOT}/components/vibe_ui/vibe_i18n.c"
    "${REPO_ROOT}/components/vibe_ui/generated/vibe_glyphs.c"
    "${REPO_ROOT}/components/vibe_ui/generated/vibe_messages.c")
target_include_directories(test_note_qr PRIVATE
    "${REPO_ROOT}/tests/host/config_stubs" "${NOTE_QR}" "${NOTE_QR}/include"
    "${REPO_ROOT}/components/app_controller/include"
    "${REPO_ROOT}/components/vibe_ui/include"
    "${REPO_ROOT}/firmware/zectrix/main"
    "${REPO_ROOT}/firmware/zectrix/components/zectrix_canvas/include")
target_compile_options(test_note_qr PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
