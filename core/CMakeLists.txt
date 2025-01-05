set(CMAKE_C_STANDARD 23)

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
    )
    if (CLAP_BUILD_FINAL)
        list(APPEND EXTRA_LIBRARIES "-flto"
                                    "-s ASSERTIONS=0"
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
    set(PLATFORM_SRC display-www.c input-www.c ui-imgui-www.c)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
else ()
    set(PLATFORM_SRC display-glfw.c)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_LIBRARIES "-fsanitize=address")
else ()
    set(DEBUG_LIBRARIES "")
endif ()

if (CLAP_BUILD_WITH_GLES)
    set(CONFIG_GLES 1)
endif ()
if (CLAP_BUILD_FINAL)
    set(CONFIG_FINAL 1)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto")
endif ()
if (CLAP_BUILD_NETWORKING)
    set(CONFIG_NETWORKING 1)
endif ()
if (CLAP_SERVER_IP)
    set(CONFIG_SERVER_IP ${CLAP_SERVER_IP})
endif ()

string(TIMESTAMP CONFIG_BUILDDATE "%Y%m%d_%H%M%S")
configure_file(config.h.in config.h)

if (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set(TEST_BIN tests)
    set(TEST_SRC test.c object.c ref.c util.c logger.c json.c messagebus.c)

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
    win32_executable(${TEST_BIN})

    add_test(${TEST_BIN} ${TEST_BIN})
endif ()

set(ENGINE_LIB libonehandclap)
set(ENGINE_SRC
    object.c ref.c util.c logger.c input.c messagebus.c
    matrix.c model.c shader.c librarian.c json.c clap.c view.c light.c
    terrain.c ui.c scene.c font.c sound.c pngloader.c base64.c
    physics.c ui-animations.c input-fuzzer.c character.c settings.c
    gltf.c input-joystick.c render-gl.c mesh.c pipeline.c input-keyboard.c
    game.c camera.c xyarray.c ca2d.c ca3d.c ui-imgui.c ui-debug.c
    ${PLATFORM_SRC})

# Mac OS X has deprecated OpenAL and is very vocal about it
set_source_files_properties(sound.c PROPERTIES COMPILE_FLAGS -Wno-deprecated-declarations)

if (NOT WIN32 AND CLAP_BUILD_NETWORKING)
    list(APPEND ENGINE_SRC networking.c)

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # sha1.c is obviously buggy, someone should do something about it
        set_source_files_properties(networking.c PROPERTIES COMPILE_FLAGS -Wno-stringop-overread)
    endif ()
endif ()

get_filename_component(PARENT_DIR ${clap_SOURCE_DIR} DIRECTORY)

# Build in ImGui via cimgui bindings
set(CIMGUI_DIR ${PARENT_DIR}/clap/deps/src/cimgui)
set(CIMGUI_BACKENDS_DIR ${CIMGUI_DIR}/imgui/backends)

set(CIMGUI_BACKEND_HEADERS
    "imgui_impl_glfw.h"
    "imgui_impl_opengl3.h"
)

# ImGui's backend headers require a bit of hackery to work with C
FOREACH(backend ${CIMGUI_BACKEND_HEADERS})
    FILE(READ "${CIMGUI_BACKENDS_DIR}/${backend}" backend_src)
    STRING(REPLACE "#include \"imgui.h\"" "" backend_src "${backend_src}")
    STRING(REPLACE "glsl_version = nullptr" "glsl_version" backend_src "${backend_src}")
    FILE(WRITE "${CMAKE_BINARY_DIR}/${backend}" "${backend_src}")
ENDFOREACH ()

file(GLOB
    CIMGUI_SRC
    "${CIMGUI_DIR}/*.cpp"
    "${CIMGUI_DIR}/imgui/*.cpp"
    "${CIMGUI_DIR}/imgui/backends/imgui_impl_glfw.cpp"
    "${CIMGUI_DIR}/imgui/backends/imgui_impl_opengl3.cpp"
)

# cimgui specific compilation defines to disable C++ mangling: this only needs
# to happen for the cimgui's compilation units, since it's C++
# Also, clang 20.0.0 detects naughtiness all over the imgui code related to
# memcpy()ing into non-trivial objects; the upstream solution seems to be to
# silence the warning:
# https://github.com/ocornut/imgui/commit/419a9ada16eb9b6d84ead4911b9c2a32820cfffb
# so, do it here, but in a less intrusive way
FOREACH(src ${CIMGUI_SRC})
    set_source_files_properties(${src} PROPERTIES
        COMPILE_DEFINITIONS "IMGUI_IMPL_API=extern\ \"C\";IMGUI_IMPL_OPENGL_LOADER_GL3W"
        COMPILE_FLAGS "-Wno-nontrivial-memaccess"
    )
ENDFOREACH ()

# the regular C sources don't need any special cimgui API prefixes
FOREACH(src ${ENGINE_SRC})
    get_source_file_property(CFLAGS ${src} COMPILE_FLAGS)
    string(REPLACE "NOTFOUND" "" CFLAGS ${CFLAGS})
    set_source_files_properties(${src} PROPERTIES
                                COMPILE_DEFINITIONS "IMGUI_IMPL_API=\ "
                                INCLUDE_DIRECTORIES "${WLIBC_INCLUDE_DIR}"
    )
    if (NOT WIN32)
        set_source_files_properties(${src} PROPERTIES
                                    COMPILE_FLAGS "-Wall -Wno-misleading-indentation -Werror ${CFLAGS}"
        )
    endif ()
ENDFOREACH ()

FILE(WRITE ${CMAKE_CURRENT_BINARY_DIR}/builddate.c "const char *build_date = \"${CONFIG_BUILDDATE}\";")

add_library(${ENGINE_LIB} ${ENGINE_SRC} ${CIMGUI_SRC} builddate.c)
add_dependencies(${ENGINE_LIB} ode)
set_target_properties(${ENGINE_LIB} PROPERTIES PREFIX "")
set_target_properties(${ENGINE_LIB} PROPERTIES CXX_STANDARD 17)
target_link_libraries(${ENGINE_LIB} PRIVATE ${FREETYPE_LIBRARIES} glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})
target_link_libraries(${ENGINE_LIB} PRIVATE ${PNG_LIBRARY} ${OPENAL_LIBRARY} ${VORBISFILE_LIBRARY})
target_link_libraries(${ENGINE_LIB} PRIVATE libode meshoptimizer)
target_link_libraries(${ENGINE_LIB} PRIVATE ${EXTRA_LIBRARIES} ${DEBUG_LIBRARIES})
target_include_directories(${ENGINE_LIB} PRIVATE ${ODE_INCLUDE})
target_include_directories(${ENGINE_LIB} PRIVATE ${FREETYPE_INCLUDE_DIRS})
target_include_directories(${ENGINE_LIB} PRIVATE ${OPENAL_INCLUDE_DIRS})
target_include_directories(${ENGINE_LIB} PRIVATE ${CIMGUI_DIR}/imgui)
target_include_directories(${ENGINE_LIB} PRIVATE ${GLFW_INCLUDES})
target_include_directories(${ENGINE_LIB} PRIVATE ${CIMGUI_DIR} ${CMAKE_BINARY_DIR})
win32_library(${ENGINE_LIB} FALSE)
