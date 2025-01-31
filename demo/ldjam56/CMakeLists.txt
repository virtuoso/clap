option(LDJAM56_BUILD_IN_ASSETS BOOL ON)

set(CMAKE_C_STANDARD 11)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(ENGINE_INCLUDE "${CMAKE_SOURCE_DIR}/core" "${clap_BINARY_DIR}/core")
set(ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/asset")

get_directory_property(CONFIG_GLES
    DIRECTORY "${CMAKE_SOURCE_DIR}/core"
    DEFINITION CONFIG_GLES)
get_directory_property(CIMGUI_DIR
    DIRECTORY "${CMAKE_SOURCE_DIR}/core"
    DEFINITION CIMGUI_DIR)

set(ASSETS  "${ASSET_DIR}/scene.json")

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
elseif (LDJAM56_BUILD_IN_ASSETS)
    # No cpio on pax on windows
    if (NOT WIN32)
        asset_pack(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/asset)
        set(ENGINE_ASSETS ${ASSET_FILE})
    endif ()
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_LIBRARIES "-fsanitize=address")
    if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
        set(DEBUG_LIBRARIES ${DEBUG_LIBRARIES} -g3 -gsource-map)
    endif ()
else ()
    set(DEBUG_LIBRARIES "")
endif ()

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set(ENGINE_BIN index) # because index.html
else ()
    set(ENGINE_BIN ldjam56)
endif ()

set(ENGINE_MAIN onehandclap.c)
set(ENGINE_LIB libonehandclap)

add_executable(${ENGINE_BIN} ${ENGINE_MAIN} ${ENGINE_ASSETS})
add_dependencies(${ENGINE_BIN} ${ENGINE_LIB} meshoptimizer)
target_include_directories(${ENGINE_BIN} PRIVATE ${ENGINE_INCLUDE} ${ODE_INCLUDE} ${CIMGUI_DIR})
set_target_properties(${ENGINE_BIN} PROPERTIES LINK_DEPENDS "${ASSETS};${CMAKE_CURRENT_SOURCE_DIR}/shell_clap.html")
target_link_libraries(${ENGINE_BIN} PRIVATE ${FREETYPE_LIBRARIES} glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})
target_link_libraries(${ENGINE_BIN} PRIVATE ${PNG_LIBRARY} ${OPENAL_LIBRARY} ${VORBISFILE_LIBRARY})
target_link_libraries(${ENGINE_BIN} PRIVATE libode meshoptimizer)
target_link_libraries(${ENGINE_BIN} PRIVATE ${EXTRA_LIBRARIES} ${DEBUG_LIBRARIES})
target_link_libraries(${ENGINE_BIN} PRIVATE ${ENGINE_LIB})
win32_executable(${ENGINE_BIN})

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        install(TARGETS ${ENGINE_BIN} DESTINATION clapdbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_BIN}.wasm.map DESTINATION clapdbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_BIN}.wasm DESTINATION clapdbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_BIN}.data DESTINATION clapdbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_BIN}.js DESTINATION clapdbg)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/background.jpg DESTINATION clapdbg)
    else ()
        if (CLAP_BUILD_FINAL)
            install(TARGETS ${ENGINE_BIN} DESTINATION clap)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_BIN}.wasm DESTINATION clap)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_BIN}.data DESTINATION clap)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_BIN}.js DESTINATION clap)
            install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/background.jpg DESTINATION clap)
        else ()
            install(TARGETS ${ENGINE_BIN} DESTINATION claptest)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_BIN}.wasm DESTINATION claptest)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_BIN}.data DESTINATION claptest)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_BIN}.js DESTINATION claptest)
            install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/background.jpg DESTINATION claptest)
        endif ()
    endif ()
else ()
    install(TARGETS ${ENGINE_BIN} DESTINATION bin)
endif ()
