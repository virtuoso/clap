# Build the Umka interpreter (https://github.com/vtereshkov/umka-lang) as a
# static library. Umka ships no CMakeLists of its own, so its sources (fetched
# by bootstrap.py into deps/src/umka) are compiled directly here.
#
# Expects UMKA_DIR to point at the checkout. Exports UMKA_INCLUDE and the
# `umka` static library target.

if (NOT DEFINED UMKA_DIR)
    message(FATAL_ERROR "umka.cmake requires UMKA_DIR to be set")
endif ()

# umka.c contains the standalone interpreter's main() and is not part of the lib
file(GLOB UMKA_SRC "${UMKA_DIR}/src/*.c")
list(FILTER UMKA_SRC EXCLUDE REGEX "/umka\\.c$")

if (NOT UMKA_SRC)
    message(FATAL_ERROR "No Umka sources found in ${UMKA_DIR}/src; was it bootstrapped?")
endif ()

add_library(umka STATIC ${UMKA_SRC})
set_target_properties(umka PROPERTIES FOLDER "ThirdPartyLibraries")

# UMKA_STATIC drops the dllimport/dllexport decorations; we do not build UMIs
# (Umka module implementation libraries), so UMKA_EXT_LIBS stays off and there is
# no dlopen/libdl dependency.
target_compile_definitions(umka PRIVATE UMKA_STATIC)
target_include_directories(umka PUBLIC "${UMKA_DIR}/src")

# Umka is third-party C that relies on type punning through unions, so it must be
# built with -fno-strict-aliasing. It is also not clean under the engine's
# -Wall -Werror, which is applied per-source to the engine's own translation
# units only, not to this target.
if (NOT WIN32)
    target_compile_options(umka PRIVATE -fno-strict-aliasing -Wno-format-security)
    # The interpreter's runtime uses libm; carry it for whoever links umka
    target_link_libraries(umka PUBLIC m)
endif ()

if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    # The engine compiles with shared memory and pthreads; build Umka's objects
    # with the same atomics/bulk-memory ABI so wasm-ld will link them together.
    target_compile_options(umka PRIVATE -pthread)
endif ()

set(UMKA_INCLUDE "${UMKA_DIR}/src")
