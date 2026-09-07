idf_component_register(
    SRCS "zectrix_canvas.cc"
         "../../../../components/vibe_ui/generated/vibe_todo_glyphs.c"
    INCLUDE_DIRS "include" "font"
    REQUIRES zectrix_epd vibe_ui
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
