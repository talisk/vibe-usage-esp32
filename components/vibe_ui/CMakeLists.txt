idf_component_register(SRCS "vibe_i18n.c" "generated/vibe_messages.c" "generated/vibe_glyphs.c"
    INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
