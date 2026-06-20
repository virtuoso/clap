# The input subsystem: the input message source, the keyboard and joystick
# mappings, the controls settings and the input fuzzer, built as a static
# library and linked into the engine. The browser and the Apple game
# controller backends come in on their platforms.

set(INPUT_SRC input.c keyboard.c joystick.c controls.c fuzzer.c)
if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    list(APPEND INPUT_SRC www.c)
endif ()
if (APPLE)
    list(APPEND INPUT_SRC apple.m)
    set_source_files_properties(apple.m PROPERTIES
        COMPILE_FLAGS "-Wno-deprecated-declarations -Wno-availability -fno-objc-arc")
endif ()

clap_subsystem_library(clap_input ${INPUT_SRC})
