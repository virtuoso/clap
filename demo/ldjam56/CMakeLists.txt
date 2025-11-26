set(CMAKE_C_STANDARD 23)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(${CMAKE_SOURCE_DIR}/scripts/executable.cmake)

clap_executable(
    clap
    "onehandclap.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/asset"
    ""
    "ofl/Unbounded-Regular.ttf"
    ${CLAP_DEMOS_BUILD_IN_ASSETS}
)

