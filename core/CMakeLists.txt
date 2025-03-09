set(CMAKE_C_STANDARD 23)

include (${CMAKE_SOURCE_DIR}/scripts/renderer-switch.cmake)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set(FREETYPE_LIBRARIES  "-s USE_FREETYPE=1")
    set(PNG_LIBRARY         "-s USE_LIBPNG=1")
    set(VORBISFILE_LIBRARY  "-s USE_VORBIS=1")
    set(OPENGL_LIBRARIES    "-s USE_WEBGL2=1 -s FULL_ES3=1")
    set(EXTRA_LIBRARIES     "-lidbfs.js"
                            "-s WASM=1"
                            "-s ALLOW_MEMORY_GROWTH"
                            "-s MAX_WEBGL_VERSION=2"
                            "-s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$ccall"
                            "-s EXPORTED_RUNTIME_METHODS=FS"
    )
    if (CLAP_BUILD_FINAL)
        list(APPEND EXTRA_LIBRARIES "-s ASSERTIONS=0"
                                    "-s BINARYEN_EXTRA_PASSES=--one-caller-inline-max-function-size=19307")
    else ()
        list(APPEND EXTRA_LIBRARIES "-s ASSERTIONS=1"
                                    "-s EXCEPTION_DEBUG=1")
    endif ()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FREETYPE_LIBRARIES} ${PNG_LIBRARY} ${VORBISFILE_LIBRARY}")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
    else ()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
    endif ()
    set(CONFIG_BROWSER 1)
    set(CONFIG_GLES 1)
    set(PLATFORM_SRC display-www.c input-www.c)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
else ()
    set(PLATFORM_SRC display-glfw.c)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_LIBRARIES "-fsanitize=address")
else ()
    set(DEBUG_LIBRARIES "-flto")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto")
endif ()

if (CLAP_BUILD_WITH_GLES)
    set(CONFIG_GLES 1)
endif ()
if (CLAP_BUILD_FINAL)
    set(CONFIG_FINAL 1)
endif ()
if (CLAP_BUILD_NETWORKING)
    set(CONFIG_NETWORKING 1)
endif ()
if (CLAP_SERVER_IP)
    set(CONFIG_SERVER_IP ${CLAP_SERVER_IP})
endif ()

configure_file(config.h.in config.h)

if (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set(TEST_BIN tests)
    set(TEST_SRC test.c object.c util.c logger.c json.c messagebus.c ca3d.c
        ca2d.c xyarray.c memory.c cpio.c builddate.c)

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC detects a false positive -Wmaybe-uninitialized in parse_string() in json.c
        # because it can't reason that sb_finish(sb) is called under the same condition as
        # sb_init(sb), so it is, in fact, always initialized at that point. The dumb fix
        # would be to set sb.start = sb.cur = throwaway_buffer and sb.end accordingly, but
        # the code is actually correct and clang is fine with it. So, shut the warning here
        # instead.
        set_source_files_properties(json.c PROPERTIES
                                    COMPILE_FLAGS -Wno-maybe-uninitialized -Wall
                                                  -Wno-misleading-indentation -Wno-comment
                                                  -Werror ${CFLAGS})
    endif ()

    add_executable(${TEST_BIN} ${TEST_SRC})
    add_dependencies(${TEST_BIN} refclasses_gen cerrs_gen force_builddate)
    win32_executable(${TEST_BIN})

    add_test(${TEST_BIN} ${TEST_BIN})
endif ()

set(ENGINE_LIB libonehandclap)
set(ENGINE_SRC
    object.c util.c logger.c input.c messagebus.c primitives.c motion.c
    model.c shader.c librarian.c json.c clap.c view.c light.c input-keyboard.c
    terrain.c ui.c scene.c font.c sound.c pngloader.c base64.c memory.c
    physics.c ui-animations.c input-fuzzer.c character.c settings.c error.c
    gltf.c input-joystick.c render-gl.c mesh.c pipeline.c pipeline-builder.c
    game.c camera.c xyarray.c ca2d.c ca3d.c cpio.c
    ${PLATFORM_SRC})

# Mac OS X has deprecated OpenAL and is very vocal about it
set_source_files_properties(sound.c PROPERTIES COMPILE_FLAGS -Wno-deprecated-declarations)

set(CERRS_H "${CMAKE_CURRENT_BINARY_DIR}/cerrs.h")

add_custom_command(
    COMMENT "Generating cerrs.h"
    OUTPUT "${CERRS_H}"
    DEPENDS error.h
    COMMAND
        ${CMAKE_COMMAND}
            -Ddir="${CMAKE_CURRENT_SOURCE_DIR}"
            -Doutput="${CERRS_H}"
            -DCLAP_BUILD_FINAL:BOOL=${CLAP_BUILD_FINAL}
            -P ${CMAKE_SOURCE_DIR}/scripts/cerr-gen.cmake
)

add_custom_target(cerrs_gen DEPENDS "${CERRS_H}")

set(REFCLASSES_H "${CMAKE_CURRENT_BINARY_DIR}/refclasses.h")

add_custom_command(
    COMMENT "Generating refclasses.h"
    OUTPUT "${REFCLASSES_H}"
    DEPENDS ${ENGINE_SRC}
    COMMAND
        ${CMAKE_COMMAND}
            -Ddir="${CMAKE_CURRENT_SOURCE_DIR}"
            -Doutput="${REFCLASSES_H}"
            -P ${CMAKE_SOURCE_DIR}/scripts/refclass-gen.cmake
)

add_custom_target(refclasses_gen DEPENDS "${REFCLASSES_H}")

if (NOT CLAP_BUILD_FINAL)
    list(APPEND ENGINE_SRC profiler.c pipeline-debug.c)
endif ()

if (NOT WIN32 AND CLAP_BUILD_NETWORKING)
    list(APPEND ENGINE_SRC networking.c)

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # sha1.c is obviously buggy, someone should do something about it
        set_source_files_properties(networking.c PROPERTIES COMPILE_FLAGS -Wno-stringop-overread)
    endif ()
endif ()

##########################################################################
# Convert engine shaders
##########################################################################
set(SHADER_BASE "${CMAKE_BINARY_DIR}")

include("${CMAKE_SOURCE_DIR}/scripts/compile-shaders.cmake")

set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADERS ui glyph model contrast sobel combine shadow debug hblur vblur downsample upsample terrain)

compile_shaders("${SHADERS}" "_core")

##########################################################################
# Convert engine shaders into C string array to build them in statically
##########################################################################
add_custom_command(
    DEPENDS preprocess_shaders_core ${SHADER_OUTS}
    COMMENT "Generating builtin shaders"
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/builtin-shaders.c"
    COMMAND
        ${CMAKE_COMMAND}
            -DHEADER=1
            -Dshaders="${SHADER_OUTS}"
            -DOUTPUT_FILE="${CMAKE_CURRENT_BINARY_DIR}/builtin-shaders.c"
            -P ${CMAKE_SOURCE_DIR}/scripts/builtin-shaders.cmake
    COMMAND
        ${CMAKE_COMMAND}
            -Dshaders="${SHADER_OUTS}"
            -DOUTPUT_FILE="${CMAKE_CURRENT_BINARY_DIR}/builtin-shaders.c"
            -P ${CMAKE_SOURCE_DIR}/scripts/builtin-shaders.cmake
    COMMAND
        ${CMAKE_COMMAND}
            -DFOOTER=1
            -Dshaders="${SHADER_OUTS}"
            -DOUTPUT_FILE="${CMAKE_CURRENT_BINARY_DIR}/builtin-shaders.c"
            -P ${CMAKE_SOURCE_DIR}/scripts/builtin-shaders.cmake
)

##########################################################################
# Build in ImGui via cimgui bindings
##########################################################################
if (NOT CLAP_BUILD_FINAL)
    set(CIMGUI_DIR ${CMAKE_SOURCE_DIR}/deps/src/cimgui)
    set(CIMGUI_BACKENDS_DIR ${CIMGUI_DIR}/imgui/backends)
    set(CIMGUI_BACKENDS glfw opengl3)

    include("${CMAKE_SOURCE_DIR}/scripts/cimgui.cmake")
endif ()

# the regular C sources don't need any special cimgui API prefixes
FOREACH(src ${ENGINE_SRC})
    get_source_file_property(CFLAGS ${src} COMPILE_FLAGS)
    string(REPLACE "NOTFOUND" "" CFLAGS ${CFLAGS})
    set_source_files_properties(${src} PROPERTIES
                                COMPILE_DEFINITIONS "${ENGINE_COMPILE_DEFINITIONS}"
                                INCLUDE_DIRECTORIES "${WLIBC_INCLUDE_DIR}"
    )
    if (NOT WIN32)
        set_source_files_properties(${src} PROPERTIES
                                    COMPILE_FLAGS "-Wall -Wno-misleading-indentation -Wno-comment -Werror ${CFLAGS}"
        )
    endif ()
ENDFOREACH ()

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/builddate.c" FORCE_REGEN
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/scripts/builddate-gen.cmake"
    COMMENT "Generating builddate.c with actual build-time timestamp"
)

add_custom_target(
    force_builddate ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/builddate.c"
    COMMENT "Forcing builddate.c update"
)

add_library(${ENGINE_LIB} ${ENGINE_SRC} ${CIMGUI_SRC} builddate.c builtin-shaders.c)
add_dependencies(${ENGINE_LIB} ode refclasses_gen cerrs_gen force_builddate)
set_target_properties(${ENGINE_LIB} PROPERTIES PREFIX "")
set_target_properties(${ENGINE_LIB} PROPERTIES CXX_STANDARD 17)
target_link_libraries(${ENGINE_LIB} PRIVATE ${FREETYPE_LIBRARIES} glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})
target_link_libraries(${ENGINE_LIB} PRIVATE ${PNG_LIBRARY} ${OPENAL_LIBRARY} ${VORBISFILE_LIBRARY})
target_link_libraries(${ENGINE_LIB} PRIVATE libode meshoptimizer)
target_link_libraries(${ENGINE_LIB} PRIVATE ${EXTRA_LIBRARIES} ${DEBUG_LIBRARIES})
target_include_directories(${ENGINE_LIB} PRIVATE ${ODE_INCLUDE})
target_include_directories(${ENGINE_LIB} PRIVATE ${GLEW_INCLUDE_DIRS})
target_include_directories(${ENGINE_LIB} PRIVATE ${FREETYPE_INCLUDE_DIRS})
target_include_directories(${ENGINE_LIB} PRIVATE ${OPENAL_INCLUDE_DIRS})
target_include_directories(${ENGINE_LIB} PRIVATE ${CIMGUI_DIR}/imgui)
target_include_directories(${ENGINE_LIB} PRIVATE ${GLFW_INCLUDES})
target_include_directories(${ENGINE_LIB} PRIVATE ${CIMGUI_DIR} ${CMAKE_BINARY_DIR})
win32_library(${ENGINE_LIB} FALSE)
