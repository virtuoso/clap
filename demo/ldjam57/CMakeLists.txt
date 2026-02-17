set(CMAKE_C_STANDARD 23)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(${CMAKE_SOURCE_DIR}/scripts/executable.cmake)

clap_executable(
    ldjam57
    "Towards the Light"
    "main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/asset"
    "scene.json;ProggyTiny.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/background.jpg"
    "ofl/Unbounded-Regular.ttf"
    "Unbounded"
    ${CLAP_DEMOS_BUILD_IN_ASSETS}
)
