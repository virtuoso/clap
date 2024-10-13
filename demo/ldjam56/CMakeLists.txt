set(CMAKE_C_STANDARD 11)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

get_filename_component(PARENT_DIR ${clap_SOURCE_DIR} DIRECTORY)
set(ENGINE_INCLUDE "${PARENT_DIR}/clap/core" "${clap_BINARY_DIR}/core")
set(ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/asset")
message("### asset dir ${ASSET_DIR} include dir ${ENGINE_INCLUDE}")

get_directory_property(CONFIG_GLES
    DIRECTORY "${CMAKE_SOURCE_DIR}/core"
    DEFINITION CONFIG_GLES)
if (CONFIG_GLES)
    set(SHADER_TYPE "glsl-es")
else ()
    set(SHADER_TYPE "glsl")
endif ()

set(SHADER_DIR "${ASSET_DIR}/${SHADER_TYPE}")
set(SHADER_SOURCE_DIR "${PARENT_DIR}/clap/shaders")
set(SHADER_PREPROCESSOR "${PARENT_DIR}/clap/scripts/preprocess_shaders.sh")
set(SHADERS "ui"
            "glyph"
            "model"
            "contrast"
            "combine"
            "debug"
            "hblur"
            "vblur"
            "terrain"
)

add_custom_target(make-shader-output-dir
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_DIR}"
)

SET(SHADER_SRCS "")
SET(SHADER_OUTS "")
FOREACH(s ${SHADERS})
    LIST(APPEND SHADER_SRCS "${SHADER_SOURCE_DIR}/${s}.vert" "${SHADER_SOURCE_DIR}/${s}.frag")
    LIST(APPEND SHADER_OUTS "${SHADER_DIR}/${s}.vert" "${SHADER_DIR}/${s}.frag")
    add_custom_command(
        OUTPUT "${SHADER_DIR}/${s}.vert" "${SHADER_DIR}/${s}.frag"
        DEPENDS make-shader-output-dir "${SHADER_PREPROCESSOR}" "${SHADER_SOURCE_DIR}/${s}.vert" "${SHADER_SOURCE_DIR}/${s}.frag"
        COMMAND "${SHADER_PREPROCESSOR}"
        ARGS -t ${SHADER_TYPE} -o ${SHADER_DIR}/ ${SHADER_SOURCE_DIR}/${s}
    )
ENDFOREACH()

add_custom_target(preprocess_shaders
    DEPENDS ${SHADER_OUTS}
    COMMENT "Preprocessing shaders"
)

set(ASSETS  "${SHADER_OUTS}"
            "${ASSET_DIR}/scene.json")

message("### assets ${ASSETS}")

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
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3") # -flto
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
else ()
    set(OpenGL_GL_PREFERENCE "LEGACY")
    find_package(glfw3)
    find_package(GLEW)
    find_package(OpenGL)
    find_package(PNG)
    find_package(Freetype)
    find_package(OpenAL)
    find_library(VORBISFILE_LIBRARY
        NAMES vorbisfile VorbisFile VORBISFILE
        PATHS
            ENV VORBISDIR
            ENV OGGDIR
            ENV SDLSOUNDDIR
            ENV SDLDIR
            /opt
        PATH_SUFFIXES
            lib
    )
    set(GLEW_LIBRARIES GLEW::GLEW)
    # set(EXTRA_LIBRARIES m pthread stdc++)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_LIBRARIES "-fsanitize=address")
else ()
    set(DEBUG_LIBRARIES "")
endif ()

message("### GLEW: ${GLEW_LIBRARIES}")

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set(ENGINE_BIN index) # because index.html
else ()
    set(ENGINE_BIN ldjam56)
endif ()

set(ENGINE_MAIN onehandclap.c)
set(ENGINE_LIB libonehandclap)

add_executable(${ENGINE_BIN} ${ENGINE_MAIN})
add_dependencies(${ENGINE_BIN} ${ENGINE_LIB} meshoptimizer preprocess_shaders)
target_include_directories(${ENGINE_BIN} PRIVATE ${ENGINE_INCLUDE} ${ODE_INCLUDE})
set_target_properties(${ENGINE_BIN} PROPERTIES LINK_DEPENDS "${ASSETS}")
target_link_libraries(${ENGINE_BIN} ${FREETYPE_LIBRARIES} glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})
target_link_libraries(${ENGINE_BIN} ${PNG_LIBRARY} ${OPENAL_LIBRARY} ${VORBISFILE_LIBRARY})
target_link_libraries(${ENGINE_BIN} ${ODE_LIBRARY} meshoptimizer)
target_link_libraries(${ENGINE_BIN} ${EXTRA_LIBRARIES})
target_link_libraries(${ENGINE_BIN} ${ENGINE_LIB})

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        install(TARGETS ${ENGINE_BIN} DESTINATION clapdbg)
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
