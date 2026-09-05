cmake_minimum_required(VERSION 3.16)

get_filename_component(VIBE_REPOSITORY_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_compile_options(
    "-ffile-prefix-map=${VIBE_REPOSITORY_ROOT}=."
    "-fmacro-prefix-map=${VIBE_REPOSITORY_ROOT}=."
)

file(STRINGS "${VIBE_REPOSITORY_ROOT}/VERSION" PROJECT_VER LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VIBE_REPOSITORY_ROOT}/VERSION")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vibe_usage_ai_passport)
