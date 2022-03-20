set(CMAKE_C_STANDARD 11)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

get_filename_component(PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
set(ASSET_DIR "${PARENT_DIR}/asset")
message("### asset dir ${ASSET_DIR}")
if (CONFIG_GLES)
    set(SHADER_DIR "${ASSET_DIR}/glsl-es")
else ()
    set(SHADER_DIR "${ASSET_DIR}/glsl")
endif ()
set(ASSETS  "${SHADER_DIR}/ui.vert"
            "${SHADER_DIR}/ui.frag"
            "${SHADER_DIR}/glyph.vert"
            "${SHADER_DIR}/glyph.frag"
            "${SHADER_DIR}/model.vert"
            "${SHADER_DIR}/model.frag"
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
                            "-s BINARYEN_EXTRA_PASSES=--one-caller-inline-max-function-size=19307"
                            # "-flto"
                            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell_clap.html"
                            "--preload-file=${ASSET_DIR}@/asset"
                            "--use-preload-cache"
    )
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FREETYPE_LIBRARIES} ${PNG_LIBRARY} ${VORBISFILE_LIBRARY} -O3") # -flto
    set(CONFIG_BROWSER 1)
    set(CONFIG_GLES 1)
    set(PLATFORM_SRC display-www.c input-www.c)
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
    set(EXTRA_LIBRARIES m pthread stdc++)
    set(PLATFORM_SRC display-glfw.c)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_LIBRARIES "-fsanitize=address")
else ()
    set(DEBUG_LIBRARIES "")
endif ()

message("### GLEW: ${GLEW_LIBRARIES}")

if (CLAP_BUILD_WITH_GLES)
    set(CONFIG_GLES 1)
endif ()

string(TIMESTAMP CONFIG_BUILDDATE "%Y%m%d_%H%M%S")
configure_file(config.h.in config.h)

if (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set(SERVER_BIN server)
    set(SERVER_SRC networking.c clap.c input.c util.c logger.c ref.c object.c
        librarian.c messagebus.c json.c input-fuzzer.c)
    add_executable(${SERVER_BIN} ${SERVER_SRC})
    target_compile_definitions(${SERVER_BIN} PUBLIC -DSERVER_STANDALONE)
endif ()

set(ENGINE_LIB libonehandclap)
set(ENGINE_SRC ${SERVER_SRC} )
set(ENGINE_SRC
    object.c ref.c util.c logger.c graphics.c input.c messagebus.c
    matrix.c model.c shader.c objfile.c librarian.c json.c clap.c
    terrain.c ui.c scene.c font.c sound.c networking.c pngloader.c
    physics.c ui-animations.c input-fuzzer.c character.c settings.c
    gltf.c input-joystick.c render-gl.c mesh.c ${PLATFORM_SRC})
add_library(${ENGINE_LIB} ${ENGINE_SRC})
add_dependencies(${ENGINE_LIB} ode)
target_link_libraries(${ENGINE_LIB} ${FREETYPE_LIBRARIES} glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})
target_link_libraries(${ENGINE_LIB} ${PNG_LIBRARY} ${OPENAL_LIBRARY} ${VORBISFILE_LIBRARY})
target_link_libraries(${ENGINE_LIB} ${ODE_LIBRARY} meshoptimizer)
target_link_libraries(${ENGINE_LIB} ${EXTRA_LIBRARIES} ${DEBUG_LIBRARIES})
target_include_directories(${ENGINE_LIB} PRIVATE ${ODE_INCLUDE})
target_include_directories(${ENGINE_LIB} PRIVATE ${FREETYPE_INCLUDE_DIRS})

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    set(ENGINE_BIN index) # because index.html
else ()
    set(ENGINE_BIN onehandclap)
endif ()

set(ENGINE_MAIN onehandclap.c builddate.c)

add_custom_command(
    OUTPUT builddate.c
    DEPENDS ${ENGINE_SRC}
    COMMAND date +'const char *build_date = \"%Y%m%d_%H%M%S\"\;' > builddate.c
)

add_executable(${ENGINE_BIN} ${ENGINE_MAIN})
add_dependencies(${ENGINE_BIN} ${ENGINE_LIB} meshoptimizer)
target_include_directories(${ENGINE_BIN} PRIVATE ${ODE_INCLUDE})
set_target_properties(${ENGINE_BIN} PROPERTIES LINK_DEPENDS "${ASSETS}")
target_link_libraries(${ENGINE_BIN} ${FREETYPE_LIBRARIES} glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})
target_link_libraries(${ENGINE_BIN} ${PNG_LIBRARY} ${OPENAL_LIBRARY} ${VORBISFILE_LIBRARY})
target_link_libraries(${ENGINE_BIN} ${ODE_LIBRARY} meshoptimizer)
target_link_libraries(${ENGINE_LIB} ${EXTRA_LIBRARIES} ${DEBUG_LIBRARIES})
target_link_libraries(${ENGINE_BIN} ${ENGINE_LIB})

if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        install(TARGETS ${ENGINE_BIN} DESTINATION clapdbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION clapdbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION clapdbg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION clapdbg)
    else ()
        install(TARGETS ${ENGINE_BIN} DESTINATION clap)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION clap)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION clap)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION clap)
    endif ()
else ()
    install(TARGETS ${ENGINE_BIN} DESTINATION bin)
endif ()
