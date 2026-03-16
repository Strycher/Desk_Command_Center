# Main component — P4 ESP-IDF build
# Only compiles P4 platform files. S3 uses Arduino framework separately.

set(MAIN_SRCS
    "platform/p4/main_p4.cpp"
    "platform/p4/display_p4.cpp"
    "platform/p4/platform_p4.cpp"
)

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "../include"
)
