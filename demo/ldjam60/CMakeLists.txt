set(CMAKE_C_STANDARD 23)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(${CMAKE_SOURCE_DIR}/scripts/executable.cmake)

clap_executable(
    ldjam60
    "This Way Up"
    "main.c;clappy.c;switch.c;rot.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/asset"
    "level1.json;ProggyTiny.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/asset/background.png"
    "ofl/Unbounded-Regular.ttf"
    "Unbounded"
    ${CLAP_DEMOS_BUILD_IN_ASSETS}
)
