set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    # Compile flags only: CMAKE_C_FLAGS would put these on the link lines as
    # well, where the ports come from CLAP_DEPENDENCY_LIBRARIES instead
    string(JOIN " " emscripten_compile_flags
        ${CLAP_EMSCRIPTEN_COMPILE_OPTIONS}
        ${CLAP_EMSCRIPTEN_COMPILE_LIBRARIES}
    )
    add_compile_options("SHELL:${emscripten_compile_flags}")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
    else ()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
    endif ()
    set(CONFIG_BROWSER 1)
    set(CONFIG_GLES 1)
    set(PLATFORM_SRC display-www.c)
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
if (CLAP_SERVER_PORT)
    set(CONFIG_SERVER_PORT ${CLAP_SERVER_PORT})
endif ()
if (CLAP_SERVER_WSPORT)
    set(CONFIG_SERVER_WSPORT ${CLAP_SERVER_WSPORT})
endif ()
if (CLAP_BUILD_WITH_SCRIPTING)
    set(CONFIG_SCRIPTING 1)
endif ()

configure_file(config.h.in config.h)

set(TEST_BIN tests)
set(TEST_SRC test.c test-harness.c object.c util.c logger.c json.c messagebus.c ca3d.c error.c
    ca2d.c xyarray.c memory.c cpio.c fs-ops.c fs-ops-posix.c
    render-texture.c draw.c os-info.c)

if (NOT WIN32 AND CLAP_BUILD_NETWORKING)
    list(APPEND TEST_SRC websocket.c)
endif ()

# The native directory-watch backend, so the fs watch test has a vtable to link
if (APPLE)
    list(APPEND TEST_SRC fs-ops-fsevents.c)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    list(APPEND TEST_SRC fs-ops-inotify.c)
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC detects a false positive -Wmaybe-uninitialized in parse_string() in json.c
    # because it can't reason that sb_finish(sb) is called under the same condition as
    # sb_init(sb), so it is, in fact, always initialized at that point. The dumb fix
    # would be to set sb.start = sb.cur = throwaway_buffer and sb.end accordingly, but
    # the code is actually correct and clang is fine with it. So, shut the warning here
    # instead.
    set_source_files_properties(json.c PROPERTIES COMPILE_FLAGS -Wno-maybe-uninitialized)
endif ()

add_executable(${TEST_BIN} ${TEST_SRC})
add_dependencies(${TEST_BIN} refclasses_gen cerrs_gen)
target_compile_definitions(${TEST_BIN} PRIVATE -DCLAP_TESTS)
if (APPLE)
    target_link_libraries(${TEST_BIN} PRIVATE "-framework CoreServices")
endif ()
target_include_directories(${TEST_BIN} PRIVATE ${UMKA_INCLUDE})
win32_executable(${TEST_BIN})
if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    # A node runnable .js entry point instead of the .html shell; ctest runs
    # it with CMAKE_CROSSCOMPILING_EMULATOR (node from the emsdk toolchain)
    set_target_properties(${TEST_BIN} PROPERTIES SUFFIX ".js")
    target_link_options(${TEST_BIN} PRIVATE ${DEBUG_LIBRARIES} ${CLAP_TEST_LINK_OPTIONS})
else ()
    target_link_options(${TEST_BIN} PRIVATE ${CLAP_LINK_OPTIONS} ${DEBUG_LIBRARIES})
endif ()

add_test(NAME ${TEST_BIN} COMMAND ${TEST_BIN})

set(RENDERER_SRC "")
if (CONFIG_RENDERER_OPENGL)
    list(APPEND RENDERER_SRC render-gl.c)
endif ()
if (CONFIG_RENDERER_METAL)
    list(APPEND RENDERER_SRC render-mtl.m display-glfw-metal.m)
endif ()
if (CONFIG_RENDERER_WGPU)
    list(APPEND RENDERER_SRC render-wgpu.c)
endif ()
if (NOT RENDERER_SRC)
    message(FATAL_ERROR "Unsupported renderer")
endif ()

set(PHYSICS_SRC "")
if (CONFIG_PHYSICS_ODE)
    list(APPEND PHYSICS_SRC physics-ode.c)
endif ()
if (CONFIG_PHYSICS_BOX3D)
    list(APPEND PHYSICS_SRC physics-box3d.c)
endif ()
if (NOT PHYSICS_SRC)
    message(FATAL_ERROR "Unsupported physics backend")
endif ()

set(MA_DIR ${CMAKE_SOURCE_DIR}/deps/src/miniaudio)

set(ENGINE_LIB libonehandclap)
set(ENGINE_SRC
    object.c util.c fs-ops.c fs-ops-posix.c logger.c messagebus.c primitives.c motion.c
    model.c shader.c librarian.c json.c clap.c view.c light.c thread-pthread.c
    terrain.c ui.c scene.c font.c sound.c pngloader.c base64.c memory.c render-common.c render-texture.c draw.c
    ${PHYSICS_SRC} ui-animations.c ui-menus.c ui-settings.c character.c gpsm.c settings.c error.c
    gltf.c mesh.c os-info.c pipeline.c pipeline-builder.c datatypes.c
    camera.c xyarray.c ca2d.c ca3d.c cpio.c particle.c ssao.c lut.c transform.c noise.c graphics-settings.c audio-settings.c
    prng.c prng-mt.cpp bsp.c graph.c
    ${PLATFORM_SRC} ${RENDERER_SRC} ${MA_DIR}/miniaudio.c ${MA_DIR}/extras/stb_vorbis.c)

set_source_files_properties(${MA_DIR}/miniaudio.c PROPERTIES COMPILE_FLAGS
    "-DMA_ENABLE_AUDIO_WORKLETS -DSTB_VORBIS_HEADER_ONLY -include ${MA_DIR}/extras/stb_vorbis.c"
)

set(CERRS_H "${CMAKE_CURRENT_BINARY_DIR}/cerrs.h")

add_custom_command(
    COMMENT "Generating cerrs.h"
    OUTPUT "${CERRS_H}"
    DEPENDS error.h ${CMAKE_SOURCE_DIR}/scripts/cerr-gen.cmake
    COMMAND
        ${CMAKE_COMMAND}
            -Ddir="${CMAKE_CURRENT_SOURCE_DIR}"
            -Doutput="${CERRS_H}"
            -DCLAP_BUILD_FINAL:BOOL=${CLAP_BUILD_FINAL}
            -P ${CMAKE_SOURCE_DIR}/scripts/cerr-gen.cmake
)

add_custom_target(cerrs_gen DEPENDS "${CERRS_H}")

set(REFCLASSES_H "${CMAKE_CURRENT_BINARY_DIR}/refclasses.h")

# DECLARE_REFCLASS() lives mostly in headers; mirror the file set
# refclass-gen.cmake trawls
file(GLOB_RECURSE REFCLASSES_GEN_DEPS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.m"
)

add_custom_command(
    COMMENT "Generating refclasses.h"
    OUTPUT "${REFCLASSES_H}"
    DEPENDS ${REFCLASSES_GEN_DEPS} ${CMAKE_SOURCE_DIR}/scripts/refclass-gen.cmake
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
    list(APPEND ENGINE_SRC networking.c websocket.c)

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # sha1.c is obviously buggy, someone should do something about it
        set_source_files_properties(networking.c PROPERTIES COMPILE_FLAGS -Wno-stringop-overread)
    endif ()
endif ()

if (NOT CLAP_BUILD_FINAL)
    list(APPEND ENGINE_SRC debug_draw.c ui-debug-fs.c)
endif ()

if (APPLE)
    list(APPEND ENGINE_SRC fs-ops-fsevents.c)
    list(APPEND EXTRA_LIBRARIES "-framework GameController" "-framework Foundation" "-framework Cocoa" "-framework CoreServices")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    list(APPEND ENGINE_SRC fs-ops-inotify.c)
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
    set(CIMGUI_BACKENDS "")
    if (CONFIG_RENDERER_OPENGL)
        list(APPEND CIMGUI_BACKENDS opengl3)
    endif ()
    if (CONFIG_RENDERER_METAL)
        list(APPEND CIMGUI_BACKENDS metal)
    endif ()
    if (CONFIG_RENDERER_WGPU)
        list(APPEND CIMGUI_BACKENDS wgpu)
    endif ()
    # glfw is used by all native (non-Emscripten) backends
    if (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
        list(APPEND CIMGUI_BACKENDS glfw)
    endif ()

    include("${CMAKE_SOURCE_DIR}/scripts/cimgui.cmake")
endif ()

# The warning set for the engine's own translation units
set(ENGINE_WARNING_FLAGS -Wall -Wno-misleading-indentation -Wno-comment -Werror)
list(JOIN ENGINE_WARNING_FLAGS " " engine_warning_flags)

# the regular C sources don't need any special cimgui API prefixes
FOREACH(src ${ENGINE_SRC})
    get_source_file_property(CFLAGS ${src} COMPILE_FLAGS)
    string(REPLACE "NOTFOUND" "" CFLAGS ${CFLAGS})
    set_source_files_properties(${src} PROPERTIES
                                COMPILE_DEFINITIONS "${ENGINE_COMPILE_DEFINITIONS}"
    )
    if (NOT WIN32)
        set_source_files_properties(${src} PROPERTIES
                                    COMPILE_FLAGS "${engine_warning_flags} ${CFLAGS}"
        )
    endif ()
ENDFOREACH ()

set_source_files_properties(render-mtl.m COMPILE_FLAGS "-Wno-deprecated-declarations -Wno-availability -fno-objc-arc")

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
add_dependencies(${ENGINE_LIB} refclasses_gen cerrs_gen force_builddate render_bindings_gen)
if (CONFIG_PHYSICS_ODE)
    add_dependencies(${ENGINE_LIB} ode)
endif ()
set_target_properties(${ENGINE_LIB} PROPERTIES PREFIX "")
set_target_properties(${ENGINE_LIB} PROPERTIES CXX_STANDARD 17)
target_link_libraries(${ENGINE_LIB} PRIVATE ${CLAP_DEPENDENCY_LIBRARIES})
target_link_libraries(${ENGINE_LIB} PRIVATE ${CLAP_RENDERER_LIBRARIES})
target_link_libraries(${ENGINE_LIB} PRIVATE ${CLAP_PHYSICS_LIBRARIES} meshoptimizer kissfft)
target_link_libraries(${ENGINE_LIB} PRIVATE ${EXTRA_LIBRARIES})
target_link_options(${ENGINE_LIB} PRIVATE ${DEBUG_LIBRARIES})
target_include_directories(${ENGINE_LIB} PRIVATE ${MA_DIR})
target_include_directories(${ENGINE_LIB} PRIVATE ${CLAP_PHYSICS_INCLUDE_DIRS})
target_include_directories(${ENGINE_LIB} PRIVATE ${KISSFFT_INCLUDE})
target_include_directories(${ENGINE_LIB} PRIVATE ${CLAP_DEPENDENCY_INCLUDE_DIRS})
target_include_directories(${ENGINE_LIB} PRIVATE ${CLAP_RENDERER_INCLUDE_DIRS})
target_include_directories(${ENGINE_LIB} PRIVATE ${CIMGUI_DIR}/imgui)
target_include_directories(${ENGINE_LIB} PRIVATE ${CIMGUI_DIR} ${CMAKE_BINARY_DIR})
win32_library(${ENGINE_LIB} FALSE)

# The input subsystem is its own static library (see input/CMakeLists.txt)
add_subdirectory(input)
target_link_libraries(${ENGINE_LIB} PRIVATE clap_input)

# The Umka scripting subsystem is its own static library (see script/CMakeLists.txt)
if (CLAP_BUILD_WITH_SCRIPTING)
    add_subdirectory(script)
    target_link_libraries(${ENGINE_LIB} PRIVATE clap_script)
endif ()

# The prng tests compile the generator sources directly and link neither
# the engine nor the main test blob
clap_subsystem_test(prng_tests test-prng.c prng.c prng-mt.cpp)

# The bsp tests compile the partitioner and the prng it splits streams from
clap_subsystem_test(bsp_tests test-bsp.c bsp.c prng.c prng-mt.cpp)

# The graph tests compile the graph and the prng its generator draws from
clap_subsystem_test(graph_tests test-graph.c graph.c prng.c prng-mt.cpp)

# The gpsm tests compile the state machine alone; the callbacks are the
# fixture's own
clap_subsystem_test(gpsm_tests test-gpsm.c gpsm.c)
