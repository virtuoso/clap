# Put the build-time date/time into a compilation unit
string(TIMESTAMP NOW "%Y%m%d_%H%M%S")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/builddate.c" "const char *build_date = \"${NOW}\";\n")

execute_process(
    COMMAND git describe --tags --always --dirty
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Add version identifier
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/builddate.c" "const char *clap_version = \"${GIT_VERSION}\";\n")
