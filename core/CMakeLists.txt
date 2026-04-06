set(CMAKE_C_STANDARD 23)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    string(JOIN " " emscripten_compile_flags
        ${CLAP_EMSCRIPTEN_COMPILE_OPTIONS}
        ${CLAP_EMSCRIPTEN_COMPILE_LIBRARIES}
    )
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${emscripten_compile_flags}")
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
    set(PLATFORM_SRC display-glfw.c loading-screen.c)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (NOT MINGW)
        set(DEBUG_LIBRARIES ${ASAN_FLAG};${UBSAN_FLAG})
    endif ()
else ()
    set(DEBUG_LIBRARIES "-flto")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto")
endif ()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")

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

if (CONFIG_RENDERER_METAL OR CONFIG_RENDERER_VULKAN OR CONFIG_RENDERER_D3D12)
    set(CONFIG_ORIGIN_TOP_LEFT 1)
    set(CONFIG_NDC_ZERO_ONE 1)
endif ()
configure_file(config.h.in config.h)

if (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set(TEST_BIN tests)
    set(TEST_SRC test.c object.c util.c logger.c json.c messagebus.c ca3d.c error.c
        ca2d.c xyarray.c memory.c cpio.c builddate.c fs-ops.c fs-ops-posix.c
        render-texture.c draw.c)

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
    target_compile_definitions(${TEST_BIN} PRIVATE -DCLAP_TESTS)
    target_link_options(${TEST_BIN} PRIVATE ${CLAP_LINK_OPTIONS} ${DEBUG_LIBRARIES})
    win32_executable(${TEST_BIN})

    add_test(${TEST_BIN} ${TEST_BIN})
endif ()

if (CONFIG_RENDERER_OPENGL)
    set(RENDERER_SRC "render-gl.c")
elseif (CONFIG_RENDERER_METAL)
    set(RENDERER_SRC render-mtl.m display-glfw-metal.m)
else ()
    message(FATAL_ERROR "Unsupported renderer")
endif ()

set(MA_DIR ${CMAKE_SOURCE_DIR}/deps/src/miniaudio)

set(ENGINE_LIB libonehandclap)
set(ENGINE_SRC
    object.c util.c fs-ops.c fs-ops-posix.c logger.c input.c messagebus.c primitives.c motion.c
    model.c shader.c librarian.c json.c clap.c view.c light.c input-keyboard.c thread-pthread.c
    terrain.c ui.c scene.c font.c sound.c pngloader.c base64.c memory.c render-common.c render-texture.c draw.c
    physics.c ui-animations.c input-fuzzer.c character.c settings.c error.c
    gltf.c input-joystick.c mesh.c pipeline.c pipeline-builder.c datatypes.c
    camera.c xyarray.c ca2d.c ca3d.c cpio.c particle.c ssao.c lut.c transform.c
    ${PLATFORM_SRC} ${RENDERER_SRC} ${MA_DIR}/miniaudio.c ${MA_DIR}/extras/stb_vorbis.c)

set_source_files_properties(${MA_DIR}/miniaudio.c PROPERTIES COMPILE_FLAGS
    "-DMA_ENABLE_AUDIO_WORKLETS -DSTB_VORBIS_HEADER_ONLY -include ${MA_DIR}/extras/stb_vorbis.c"
)

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

set(RENDER_BINDINGS_DIR "${CMAKE_CURRENT_BINARY_DIR}/bindings")
set(GLSL_UBO_DIR "${CMAKE_BINARY_DIR}/glsl-ubos")
set(RENDER_BINDINGS_H "${RENDER_BINDINGS_DIR}/render-bindings.h")
set(RENDER_BINDINGS_C "${RENDER_BINDINGS_DIR}/render-bindings.c")
set(RENDER_BINDINGS_COMMON_H "${RENDER_BINDINGS_DIR}/render-bindings-common.h")
set(RENDER_BINDINGS_GL_H "${RENDER_BINDINGS_DIR}/render-bindings-gl.h")
set(RENDER_BINDINGS_METAL_H "${RENDER_BINDINGS_DIR}/render-bindings-metal.h")
set(RENDER_BINDINGS_WGPU_H "${RENDER_BINDINGS_DIR}/render-bindings-wgpu.h")

add_custom_command(
    COMMENT "Generating render bindings"
    OUTPUT
        "${RENDER_BINDINGS_H}"
        "${RENDER_BINDINGS_C}"
        "${RENDER_BINDINGS_COMMON_H}"
        "${RENDER_BINDINGS_GL_H}"
        "${RENDER_BINDINGS_METAL_H}"
        "${RENDER_BINDINGS_WGPU_H}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/render-bindings.json"
        "${CMAKE_SOURCE_DIR}/scripts/render-bindings-gen.cmake"
    COMMAND
        ${CMAKE_COMMAND}
            -Dinput="${CMAKE_CURRENT_SOURCE_DIR}/render-bindings.json"
            -Doutput_dir="${RENDER_BINDINGS_DIR}"
            -Dglsl_ubo_dir="${GLSL_UBO_DIR}"
            -P "${CMAKE_SOURCE_DIR}/scripts/render-bindings-gen.cmake"
)

add_custom_target(render_bindings_gen DEPENDS
    "${RENDER_BINDINGS_H}"
    "${RENDER_BINDINGS_C}"
    "${RENDER_BINDINGS_COMMON_H}"
    "${RENDER_BINDINGS_GL_H}"
    "${RENDER_BINDINGS_METAL_H}"
    "${RENDER_BINDINGS_WGPU_H}"
)

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

if (NOT CLAP_BUILD_FINAL)
    list(APPEND ENGINE_SRC debug_draw.c ui-debug-fs.c)
endif ()

if (APPLE)
    list(APPEND ENGINE_SRC input-apple.m)
    list(APPEND EXTRA_LIBRARIES "-framework GameController" "-framework Foundation" "-framework Cocoa")
endif ()

##########################################################################
# Convert engine shaders
##########################################################################
set(SHADER_BASE "${CMAKE_BINARY_DIR}")

include("${CMAKE_SOURCE_DIR}/scripts/compile-shaders.cmake")

set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADERS ui glyph model contrast sobel sobel-msaa laplace
    smaa-blend-weights smaa-neighborhood-blend combine shadow
    debug hblur vblur downsample upsample terrain particle ssao
    shadow_vsm)

compile_shaders("${SHADERS}" "_core")
add_dependencies(preprocess_shaders_core render_bindings_gen)

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
    if (CONFIG_RENDERER_OPENGL)
        set(CIMGUI_BACKENDS glfw opengl3)
    elseif (CONFIG_RENDERER_METAL)
        set(CIMGUI_BACKENDS glfw metal)
    else ()
        message(FATAL_ERROR "Unsupported renderer")
    endif ()

    include("${CMAKE_SOURCE_DIR}/scripts/cimgui.cmake")
endif ()

# the regular C sources don't need any special cimgui API prefixes
FOREACH(src ${ENGINE_SRC})
    get_source_file_property(CFLAGS ${src} COMPILE_FLAGS)
    string(REPLACE "NOTFOUND" "" CFLAGS ${CFLAGS})
    set_source_files_properties(${src} PROPERTIES
                                COMPILE_DEFINITIONS "${ENGINE_COMPILE_DEFINITIONS}"
    )
    if (NOT WIN32)
        set_source_files_properties(${src} PROPERTIES
                                    COMPILE_FLAGS "-Wall -Wno-misleading-indentation -Wno-comment -Werror ${CFLAGS}"
        )
    endif ()
ENDFOREACH ()

set_source_files_properties(render-mtl.m COMPILE_FLAGS "-Wno-deprecated-declarations -Wno-availability -fno-objc-arc")
set_source_files_properties(input-apple.m COMPILE_FLAGS "-Wno-deprecated-declarations -Wno-availability -fno-objc-arc")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/builddate.c" FORCE_REGEN
    COMMAND ${CMAKE_COMMAND}
    ARGS
        -DGIT_VERSION="${GIT_VERSION}"
        -P "${CMAKE_SOURCE_DIR}/scripts/builddate-gen.cmake"
    COMMENT "Generating builddate.c with actual build-time timestamp"
)

add_custom_target(
    force_builddate ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/builddate.c"
    COMMENT "Forcing builddate.c update"
)

add_library(${ENGINE_LIB} ${ENGINE_SRC} ${CIMGUI_SRC} builddate.c builtin-shaders.c)
add_dependencies(${ENGINE_LIB} ode refclasses_gen cerrs_gen force_builddate render_bindings_gen)
set_target_properties(${ENGINE_LIB} PROPERTIES PREFIX "")
set_target_properties(${ENGINE_LIB} PROPERTIES CXX_STANDARD 17)
target_link_libraries(${ENGINE_LIB} PRIVATE ${CLAP_DEPENDENCY_LIBRARIES})
target_link_libraries(${ENGINE_LIB} PRIVATE ${CLAP_RENDERER_LIBRARIES})
target_link_libraries(${ENGINE_LIB} PRIVATE libode meshoptimizer)
target_link_libraries(${ENGINE_LIB} PRIVATE ${EXTRA_LIBRARIES})
target_link_options(${ENGINE_LIB} PRIVATE ${DEBUG_LIBRARIES})
target_include_directories(${ENGINE_LIB} PRIVATE ${MA_DIR})
target_include_directories(${ENGINE_LIB} PRIVATE ${ODE_INCLUDE})
target_include_directories(${ENGINE_LIB} PRIVATE ${CLAP_DEPENDENCY_INCLUDE_DIRS})
target_include_directories(${ENGINE_LIB} PRIVATE ${CLAP_RENDERER_INCLUDE_DIRS})
target_include_directories(${ENGINE_LIB} PRIVATE ${CIMGUI_DIR}/imgui)
target_include_directories(${ENGINE_LIB} PRIVATE ${CIMGUI_DIR} ${CMAKE_BINARY_DIR})
win32_library(${ENGINE_LIB} FALSE)
