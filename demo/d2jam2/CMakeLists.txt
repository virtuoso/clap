set(CMAKE_C_STANDARD 23)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(${CMAKE_SOURCE_DIR}/scripts/executable.cmake)

clap_executable(
    d2jam2
    "Cave Gathering"
    "main.c;mesh-noise.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/asset"
    "cave.json;brass attack.ogg;gamecontrollerdb.txt;ofl/ZillaSlab-Bold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/asset/background.png"
    "ofl/Chivo[wght].ttf"
    "Chivo"
    ${CLAP_DEMOS_BUILD_IN_ASSETS}
)
