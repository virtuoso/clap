set(CMAKE_C_STANDARD 11)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

get_filename_component(PARENT_DIR ${clap_SOURCE_DIR} DIRECTORY)
set(ENGINE_INCLUDE "${PARENT_DIR}/clap/core" "${clap_BINARY_DIR}/core")
set(ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/asset")

get_directory_property(CONFIG_GLES
    DIRECTORY "${CMAKE_SOURCE_DIR}/core"
    DEFINITION CONFIG_GLES)
get_directory_property(CIMGUI_DIR
    DIRECTORY "${CMAKE_SOURCE_DIR}/core"
    DEFINITION CIMGUI_DIR)
if (CONFIG_GLES)
    set(SHADER_TYPE "glsl-es")
    set(SPIRV_CROSS_ARGS --es --version 300)
else ()
    set(SHADER_TYPE "glsl")
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(SPIRV_CROSS_ARGS --no-es --no-420pack-extension --version 410)
    else ()
        set(SPIRV_CROSS_ARGS --no-es --version 410)
    endif ()
endif ()

set(SHADER_DIR "${ASSET_DIR}/${SHADER_TYPE}")
set(SHADER_SOURCE_DIR "${PARENT_DIR}/clap/shaders")
set(SPIRV_DIR "${clap_BINARY_DIR}/spirv")
set(SHADERS "ui"
            "glyph"
            "model"
            "contrast"
            "sobel"
            "combine"
            "shadow"
            "debug"
            "hblur"
            "vblur"
            "terrain"
)

add_custom_target(make-shader-ir-dir
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPIRV_DIR}"
)
add_custom_target(make-shader-output-dir
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_DIR}"
)

find_program(GLSLC glslc HINTS "${GLSLC_HINT}")
find_program(SPIRV_CROSS spirv-cross HINTS "${SPIRV_CROSS_HINT}")
SET(SHADER_SRCS "")
SET(SHADER_OUTS "")
FOREACH(s ${SHADERS})
    FOREACH(stage vert frag geom)
        # Geometry shaders are optional in GL and not supported in GL ES
        if (stage STREQUAL "geom")
            if (NOT EXISTS "${SHADER_SOURCE_DIR}/${s}.${stage}" OR CONFIG_GLES)
                continue ()
            endif ()
        endif ()
        LIST(APPEND SHADER_SRCS "${SHADER_SOURCE_DIR}/${s}.${stage}")
        LIST(APPEND SHADER_OUTS "${SHADER_DIR}/${s}.${stage}")
        add_custom_command(
            OUTPUT "${SPIRV_DIR}/${s}.${stage}.spv"
            DEPENDS make-shader-ir-dir "${SHADER_SOURCE_DIR}/${s}.${stage}" "${SHADER_SOURCE_DIR}/texel_fetch.inc"
            COMMAND "${GLSLC}"
            ARGS -fauto-map-locations --target-env=opengl -fshader-stage=${stage} -I${CMAKE_BINARY_DIR}/core -I${CMAKE_SOURCE_DIR}/core -o ${SPIRV_DIR}/${s}.${stage}.spv -c ${SHADER_SOURCE_DIR}/${s}.${stage}
        )
        add_custom_command(
            OUTPUT "${SHADER_DIR}/${s}.${stage}"
            DEPENDS make-shader-output-dir "${SPIRV_DIR}/${s}.${stage}.spv"
            COMMAND "${SPIRV_CROSS}"
            ARGS ${SPIRV_CROSS_ARGS} ${SPIRV_DIR}/${s}.${stage}.spv --output ${SHADER_DIR}/${s}.${stage}
        )
    ENDFOREACH ()
ENDFOREACH()

add_custom_target(preprocess_shaders
    DEPENDS ${SHADER_OUTS}
    COMMENT "Preprocessing shaders"
)

set(ASSETS  "${SHADER_OUTS}"
            "${ASSET_DIR}/scene.json")

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set(FREETYPE_LIBRARIES  "-s USE_FREETYPE=1")
    set(PNG_LIBRARY         "-s USE_LIBPNG=1")
    set(VORBISFILE_LIBRARY  "-s USE_VORBIS=1")
    set(OPENGL_LIBRARIES    "-s USE_WEBGL2=1 -s FULL_ES3=1")
    set(EXTRA_LIBRARIES     "-lidbfs.js"
                            "-s WASM=1"
                            "-s EXIT_RUNTIME=1"
                            "-s ALLOW_MEMORY_GROWTH"
                            "-s INITIAL_MEMORY=134217728"
                            # "-s INITIAL_MEMORY=536870912"
                            "-s MAX_WEBGL_VERSION=2"
                            "-s ASSERTIONS=1"
                            "-s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$ccall"
                            "-s BINARYEN_EXTRA_PASSES=--one-caller-inline-max-function-size=19307"
                            # "-flto"
                            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell_clap.html"
                            "--preload-file=${ASSET_DIR}@/asset"
                            # "--use-preload-cache"
    )
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
    else ()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3") # -flto
    endif ()
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
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

add_executable(${ENGINE_BIN} ${ENGINE_MAIN})
add_dependencies(${ENGINE_BIN} ${ENGINE_LIB} meshoptimizer preprocess_shaders)
target_include_directories(${ENGINE_BIN} PRIVATE ${ENGINE_INCLUDE} ${ODE_INCLUDE} ${CIMGUI_DIR})
set_target_properties(${ENGINE_BIN} PROPERTIES LINK_DEPENDS "${ASSETS}")
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
