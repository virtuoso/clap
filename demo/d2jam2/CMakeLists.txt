option(D2JAM2_BUILD_IN_ASSETS BOOL ON)

set(CMAKE_C_STANDARD 23)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(ENGINE_INCLUDE "${CMAKE_SOURCE_DIR}/core" "${clap_BINARY_DIR}/core")
set(ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/asset")

get_directory_property(CONFIG_GLES
    DIRECTORY "${CMAKE_SOURCE_DIR}/core"
    DEFINITION CONFIG_GLES)
get_directory_property(CIMGUI_DIR
    DIRECTORY "${CMAKE_SOURCE_DIR}/core"
    DEFINITION CIMGUI_DIR)

set(ASSETS  "${ASSET_DIR}/cave.json")

include(${CMAKE_SOURCE_DIR}/scripts/pack-assets.cmake)

if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    set(EXTRA_LIBRARIES     "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell_clap.html"
                            "--preload-file=${ASSET_DIR}@/asset")

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
    else ()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
    endif ()
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
elseif (D2JAM2_BUILD_IN_ASSETS)
    asset_pack(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/asset)
    set(ENGINE_ASSETS ${ASSET_FILE})
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (NOT MINGW)
        set(DEBUG_LIBRARIES ${ASAN_FLAG};${UBSAN_FLAG})
    endif ()
    if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
        set(DEBUG_LIBRARIES ${DEBUG_LIBRARIES} -g3 -gsource-map)
    endif ()
else ()
    set(DEBUG_LIBRARIES "")
endif ()

set(D2JAM2_BIN d2jam2)

set(ENGINE_MAIN main.c ui.c mesh-noise.c)
set(ENGINE_LIB libonehandclap)

add_executable(${D2JAM2_BIN} ${ENGINE_MAIN} ${ENGINE_ASSETS})
add_dependencies(${D2JAM2_BIN} ${ENGINE_LIB} meshoptimizer)
target_include_directories(${D2JAM2_BIN} PRIVATE ${ENGINE_INCLUDE} ${ODE_INCLUDE} ${CIMGUI_DIR})
set_target_properties(${D2JAM2_BIN} PROPERTIES COMPILE_FLAGS "-Wall -Wno-misleading-indentation -Wno-comment -Werror")
set_target_properties(${D2JAM2_BIN} PROPERTIES LINK_DEPENDS "${ASSETS};${CMAKE_CURRENT_SOURCE_DIR}/shell_clap.html")
target_link_libraries(${D2JAM2_BIN} PRIVATE ${FREETYPE_LIBRARIES} glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})
target_link_libraries(${D2JAM2_BIN} PRIVATE ${PNG_LIBRARY} ${OPENAL_LIBRARY} ${VORBISFILE_LIBRARY})
target_link_libraries(${D2JAM2_BIN} PRIVATE libode meshoptimizer)
target_link_libraries(${D2JAM2_BIN} PRIVATE ${EXTRA_LIBRARIES})
target_link_libraries(${D2JAM2_BIN} PRIVATE ${ENGINE_LIB})
target_link_options(${D2JAM2_BIN} PRIVATE ${DEBUG_LIBRARIES})
win32_executable(${D2JAM2_BIN})

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set_target_properties(${D2JAM2_BIN} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        OUTPUT_NAME index
    )
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        install(TARGETS ${D2JAM2_BIN} DESTINATION d2jam2dbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm.map DESTINATION d2jam2dbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION d2jam2dbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION d2jam2dbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION d2jam2dbg)
        install(FILES ${ASSET_DIR}/background.png DESTINATION d2jam2dbg)
    else ()
        if (CLAP_BUILD_FINAL)
            install(TARGETS ${D2JAM2_BIN} DESTINATION d2jam2)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION d2jam2)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION d2jam2)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION d2jam2)
            install(FILES ${ASSET_DIR}/background.png DESTINATION d2jam2)
        else ()
            install(TARGETS ${D2JAM2_BIN} DESTINATION d2jam2test)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION d2jam2test)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION d2jam2test)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION d2jam2test)
            install(FILES ${ASSET_DIR}/background.png DESTINATION d2jam2test)
        endif ()
    endif ()
else ()
    install(TARGETS ${D2JAM2_BIN} DESTINATION bin)
endif ()
