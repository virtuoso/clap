option(LDJAM57_BUILD_IN_ASSETS BOOL ON)

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
elseif (LDJAM57_BUILD_IN_ASSETS)
    asset_pack(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/asset)
    set(LDJAM57_ASSETS ${ASSET_FILE})
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_LIBRARIES "-fsanitize=address")
    if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
        set(DEBUG_LIBRARIES ${DEBUG_LIBRARIES} -g3 -gsource-map)
    endif ()
else ()
    set(DEBUG_LIBRARIES "")
endif ()

set(LDJAM57_BIN ldjam57)

set(LDJAM57_MAIN main.c)
set(ENGINE_LIB libonehandclap)

add_executable(${LDJAM57_BIN} ${LDJAM57_MAIN} ${LDJAM57_ASSETS})
add_dependencies(${LDJAM57_BIN} ${ENGINE_LIB} meshoptimizer)
target_include_directories(${LDJAM57_BIN} PRIVATE ${ENGINE_INCLUDE} ${ODE_INCLUDE} ${CIMGUI_DIR})
set_target_properties(${LDJAM57_BIN} PROPERTIES LINK_DEPENDS "${ASSETS};${CMAKE_CURRENT_SOURCE_DIR}/shell_clap.html")
target_link_libraries(${LDJAM57_BIN} PRIVATE ${FREETYPE_LIBRARIES} glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})
target_link_libraries(${LDJAM57_BIN} PRIVATE ${PNG_LIBRARY} ${OPENAL_LIBRARY} ${VORBISFILE_LIBRARY})
target_link_libraries(${LDJAM57_BIN} PRIVATE libode meshoptimizer)
target_link_libraries(${LDJAM57_BIN} PRIVATE ${EXTRA_LIBRARIES} ${DEBUG_LIBRARIES})
target_link_libraries(${LDJAM57_BIN} PRIVATE ${ENGINE_LIB})
win32_executable(${LDJAM57_BIN})

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set_target_properties(${LDJAM57_BIN} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        OUTPUT_NAME index
    )
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        install(TARGETS ${LDJAM57_BIN} DESTINATION ldjam57dbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm.map DESTINATION ldjam57dbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION ldjam57dbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION ldjam57dbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION ldjam57dbg)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/background.jpg DESTINATION ldjam57dbg)
    else ()
        if (CLAP_BUILD_FINAL)
            install(TARGETS ${LDJAM57_BIN} DESTINATION ldjam57)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION ldjam57)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION ldjam57)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION ldjam57)
            install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/background.jpg DESTINATION ldjam57)
        else ()
            install(TARGETS ${LDJAM57_BIN} DESTINATION ldjam57test)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION ldjam57test)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION ldjam57test)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION ldjam57test)
            install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/background.jpg DESTINATION ldjam57test)
        endif ()
    endif ()
else ()
    install(TARGETS ${LDJAM57_BIN} DESTINATION bin)
endif ()
