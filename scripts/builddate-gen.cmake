# Put the build-time date/time into a compilation unit
string(TIMESTAMP NOW "%Y%m%d_%H%M%S")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/builddate.c" "const char *build_date = \"${NOW}\";\n")
