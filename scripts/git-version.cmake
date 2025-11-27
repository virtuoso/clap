# Fetch full git annotated version string <tag>[-<commits since>-g<top hash>[-dirty]]
# into GIT_VERSION
execute_process(
    COMMAND git describe --tags --always --dirty
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Fetch closest tag only into GIT_SHORT_VERSION
execute_process(
    COMMAND git describe --abbrev=0
    OUTPUT_VARIABLE GIT_SHORT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Strip leading "v" and any trailing non-numeric characters (which shouldn't
# be there, but to be on the safe side) from the short version number into
# GIT_SOVERSION for cmake project version and library ABI version
string(REGEX REPLACE "v([0-9]+).*" "\\1" GIT_SOVERSION ${GIT_SHORT_VERSION})
