option(D2JAM2_FOG_PARTICLES "Build the cave fog particle system" OFF)

set(CMAKE_C_STANDARD 23)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(${CMAKE_SOURCE_DIR}/scripts/executable.cmake)

set(D2JAM2_SOURCES main.c mesh-noise.c)
if (D2JAM2_FOG_PARTICLES)
    list(APPEND D2JAM2_SOURCES fog.c)
endif ()

clap_executable(
    d2jam2
    "Cave Gathering"
    "${D2JAM2_SOURCES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/asset"
    "cave.json;brass attack.ogg;gamecontrollerdb.txt;ofl/ZillaSlab-Bold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/asset/background.png"
    "ofl/Chivo[wght].ttf"
    "Chivo"
    ${CLAP_DEMOS_BUILD_IN_ASSETS}
)

if (D2JAM2_FOG_PARTICLES)
    target_compile_definitions(d2jam2 PRIVATE D2JAM2_FOG_PARTICLES)
endif ()
