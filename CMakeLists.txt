cmake_minimum_required(VERSION 3.21)

include(scripts/git-version.cmake)

project(
    clap
    VERSION "${GIT_SOVERSION}"
)

enable_language(C)

include (FindPkgConfig)

option(CLAP_BUILD_WITH_GLES BOOL OFF)
option(CLAP_BUILD_FINAL BOOL OFF)
option(CLAP_BUILD_NETWORKING BOOL OFF)
option(CLAP_USE_UBSAN BOOL OFF)
option(CLAP_USE_ASAN BOOL OFF)
option(CLAP_DEMOS_BUILD_IN_ASSETS BOOL ON)
option(CLAP_SHIP BOOL OFF)
set(CLAP_SERVER_IP "127.0.0.1" CACHE STRING "Server IP address")
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    option(CLAP_MACOS_CREATE_DMG "Create per-executable DMG images during install" ON)
    set(CLAP_MACOS_CODESIGN_IDENTITY "" CACHE STRING "MacOS codesign identity passed to codesign -s")
    set(CLAP_MACOS_NOTARYTOOL_PROFILE "" CACHE STRING "MacOS notarytool keychain profile")
endif ()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
include(ProcessorCount)
ProcessorCount(nproc)
include(scripts/renderer-switch.cmake)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (CLAP_USE_ASAN)
        set(ASAN_FLAG "-fsanitize=address")
    endif ()
    if (CLAP_USE_UBSAN)
        set(UBSAN_FLAG "-fsanitize=undefined")
    endif ()

    set(CARGO_RELEASE_FLAG "" CACHE INTERNAL "")
    set(TARGET_DIR "debug" CACHE INTERNAL "")
    if (WIN32)
        set(CMAKE_C_FLAGS "-DCLAP_DEBUG")
        set(CMAKE_CXX_FLAGS "-DCLAP_DEBUG")
        set(W32LIBSUFFIX "d")
    else ()
        set(CMAKE_C_FLAGS "-ggdb -fno-omit-frame-pointer -DCLAP_DEBUG ${ASAN_FLAG} ${UBSAN_FLAG}")
        set(CMAKE_CXX_FLAGS "-ggdb -fno-omit-frame-pointer -DCLAP_DEBUG ${ASAN_FLAG} ${UBSAN_FLAG}")
        set(W32LIBSUFFIX "")
    endif ()
else ()
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        add_link_options("-flto=full")
    else ()
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
            add_link_options("-flto=full")
        else ()
            # Apple's linker doesn't seem to support this
            add_link_options(
                "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/thinlto-cache"
                "-Wl,--thinlto-cache-policy,cache_size_bytes=4294967296"
                "-Wl,--thinlto-jobs=${nproc}"
                "-Wl,--lto-O1"
                "-Wl,--threads=${nproc}"
            )
        endif ()
    endif ()
    set(CARGO_RELEASE_FLAG "--release" CACHE INTERNAL "")
    set(TARGET_DIR "release" CACHE INTERNAL "")
endif ()

include(scripts/bootstrap.cmake)
bootstrap_deps("bootstrap.json")

add_subdirectory(${CMAKE_SOURCE_DIR}/compat)

include(scripts/win32-helpers.cmake)

set(CLAP_DEPENDENCY_INCLUDE_DIRS "")
set(CLAP_DEPENDENCY_LIBRARIES "")
set(CLAP_RENDERER_INCLUDE_DIRS "")
set(CLAP_RENDERER_LIBRARIES "")
set(CLAP_LINK_OPTIONS "")
set(CLAP_EXECUTABLE_LINK_OPTIONS "")
set(CLAP_EMSCRIPTEN_COMPILE_OPTIONS "")
set(CLAP_EMSCRIPTEN_COMPILE_LIBRARIES "")

if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    set(CLAP_EMSCRIPTEN_COMPILE_LIBRARIES
        "-s USE_FREETYPE=1"
        "-s USE_LIBPNG=1"
    )
    set(CLAP_DEPENDENCY_LIBRARIES
        ${CLAP_EMSCRIPTEN_COMPILE_LIBRARIES}
        "-lidbfs.js"
        "-s WASM=1"
        "-s ALLOW_MEMORY_GROWTH=0"
        "-s INITIAL_MEMORY=268435456"
        "-s STACK_SIZE=131072"
        "-s MAX_WEBGL_VERSION=2"
        "-s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$ccall"
        "-s EXPORTED_RUNTIME_METHODS=FS"
        "-s AUDIO_WORKLET=1"
        "-s WASM_WORKERS=1"
        "-s SHARED_MEMORY=1"
        "-s ASYNCIFY"
        "-s USE_PTHREADS=1"
        "-s PTHREAD_POOL_SIZE=4"
    )
    set(CLAP_RENDERER_LIBRARIES
        "-s USE_WEBGL2=1"
        "-s FULL_ES3=1"
    )
    set(CLAP_EMSCRIPTEN_COMPILE_OPTIONS
        "-s WASM_WORKERS=1"
        "-s SHARED_MEMORY=1"
        "-s USE_PTHREADS=1"
    )
    if (CLAP_BUILD_FINAL)
        list(APPEND CLAP_DEPENDENCY_LIBRARIES
            "-s ASSERTIONS=0"
            "-s BINARYEN_EXTRA_PASSES=--one-caller-inline-max-function-size=19307"
        )
    else ()
        list(APPEND CLAP_DEPENDENCY_LIBRARIES
            "-s ASSERTIONS=1"
            "-s EXCEPTION_DEBUG=1"
        )
    endif ()
    set(EXTRA_LIBRARIES "")
elseif (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    enable_language(ASM)
    set(OpenGL_GL_PREFERENCE "LEGACY")
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

    set(CLAP_REQUIRE_FULLY_STATIC_LINKAGE FALSE)
    set(CLAP_REQUIRE_STATIC_ARCHIVES FALSE)
    set(CLAP_ALLOW_DYNAMIC_RUNTIME_LIBS FALSE)
    if (CLAP_SHIP AND UNIX AND NOT APPLE AND NOT WIN32 AND NOT ASAN_FLAG AND NOT UBSAN_FLAG)
        set(CLAP_REQUIRE_STATIC_ARCHIVES TRUE)
        set(CLAP_REQUIRE_FULLY_STATIC_LINKAGE TRUE)
        if (CONFIG_RENDERER_OPENGL)
            set(CLAP_ALLOW_DYNAMIC_RUNTIME_LIBS TRUE)
            message(STATUS
                "static-linkage: shipping desktop OpenGL binaries without -static; "
                "GPU drivers still require dynamic loading")
        endif ()
    elseif (CLAP_SHIP AND (ASAN_FLAG OR UBSAN_FLAG))
        message(STATUS "static-linkage: shipping mode requested, but sanitizers require dynamic linking")
    endif ()

    if (CLAP_REQUIRE_STATIC_ARCHIVES)
        if (DEFINED ENV{VCPKG_INSTALLATION_ROOT} AND EXISTS "$ENV{VCPKG_INSTALLATION_ROOT}")
            set(clap_vcpkg_root "$ENV{VCPKG_INSTALLATION_ROOT}")
        endif ()

        if (clap_vcpkg_root)
            set(clap_pkg_config_paths "")
            foreach(candidate
                    "${clap_vcpkg_root}/installed/x64-linux/lib/pkgconfig"
                    "${clap_vcpkg_root}/installed/x64-linux/share/pkgconfig")
                if (IS_DIRECTORY "${candidate}")
                    list(APPEND clap_pkg_config_paths "${candidate}")
                endif ()
            endforeach ()

            if (clap_pkg_config_paths)
                if (DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
                    string(REPLACE ":" ";" env_pkg_config_paths "$ENV{PKG_CONFIG_PATH}")
                    list(APPEND clap_pkg_config_paths ${env_pkg_config_paths})
                endif ()
                list(REMOVE_DUPLICATES clap_pkg_config_paths)
                string(JOIN ":" CLAP_PKG_CONFIG_ENV_PATH ${clap_pkg_config_paths})
                string(JOIN ", " clap_pkg_config_paths_str ${clap_pkg_config_paths})
                message(STATUS "static-linkage: pkg-config search path += ${clap_pkg_config_paths_str}")
            endif ()
        endif ()
    endif ()

    include(scripts/static-linkage.cmake)

    if (CONFIG_RENDERER_OPENGL AND NOT APPLE AND NOT CLAP_BUILD_WITH_GLES)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
        set(GLAD_API "gl=4.1" CACHE STRING "" FORCE)
        set(GLAD_GENERATOR "c" CACHE STRING "" FORCE)
        set(GLAD_PROFILE "core" CACHE STRING "" FORCE)
        set(GLAD_SPEC "gl" CACHE STRING "" FORCE)
        set(GLAD_NO_LOADER ON CACHE BOOL "" FORCE)
        set(GLAD_REPRODUCIBLE ON CACHE BOOL "" FORCE)
        set(GLAD_EXPORT OFF CACHE BOOL "" FORCE)
        set(GLAD_INSTALL OFF CACHE BOOL "" FORCE)
        add_subdirectory(${CMAKE_SOURCE_DIR}/deps/src/glad EXCLUDE_FROM_ALL)
        set_property(TARGET glad PROPERTY FOLDER "ThirdPartyLibraries")
    endif ()

    find_static_lib(glfw3 glfw3)
    find_static_lib(PNG libpng)
    find_static_lib(Freetype freetype2)

    set(PNG_LIBRARY ${PNG_LIBRARIES})

    set(CLAP_DEPENDENCY_INCLUDE_DIRS
        ${FREETYPE_INCLUDE_DIRS}
        ${PNG_INCLUDE_DIRS}
        ${glfw3_INCLUDE_DIRS}
    )
    set(CLAP_DEPENDENCY_LIBRARIES
        ${FREETYPE_LIBRARIES}
        ${glfw3_LIBRARIES}
        ${PNG_LIBRARY}
    )

    if (CONFIG_RENDERER_OPENGL)
        if (APPLE)
            find_package(OpenGL REQUIRED)
            set(CLAP_RENDERER_INCLUDE_DIRS "")
            set(CLAP_RENDERER_LIBRARIES ${OPENGL_LIBRARIES})
        elseif (CLAP_BUILD_WITH_GLES)
            set(CLAP_RENDERER_INCLUDE_DIRS "")
            set(CLAP_RENDERER_LIBRARIES "")
        elseif (WIN32)
            find_package(OpenGL REQUIRED)
            set(CLAP_RENDERER_LIBRARIES
                glad
                ${OPENGL_LIBRARIES}
            )
        else ()
            set(CLAP_RENDERER_INCLUDE_DIRS "")
            set(CLAP_RENDERER_LIBRARIES glad)
        endif ()
    elseif (CONFIG_RENDERER_METAL)
        set(CLAP_RENDERER_INCLUDE_DIRS "")
        set(CLAP_RENDERER_LIBRARIES
            "-framework Metal"
            "-framework MetalKit"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
            "-framework QuartzCore"
        )
    endif ()

    if (CLAP_ALLOW_DYNAMIC_RUNTIME_LIBS)
        _clap_dynamicize_linux_runtime_libs(CLAP_DEPENDENCY_LIBRARIES ${CLAP_DEPENDENCY_LIBRARIES})
        _clap_dynamicize_linux_runtime_libs(CLAP_RENDERER_LIBRARIES ${CLAP_RENDERER_LIBRARIES})
    endif ()

    list(REMOVE_DUPLICATES CLAP_DEPENDENCY_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES CLAP_RENDERER_INCLUDE_DIRS)

    if (WIN32 AND NOT MINGW)
        set(EXTRA_LIBRARIES ntdll Netapi32)
    elseif (APPLE)
        set(EXTRA_LIBRARIES "")
    elseif (NOT WIN32)
        set(EXTRA_LIBRARIES m pthread)
        if (CLAP_REQUIRE_FULLY_STATIC_LINKAGE)
            set(CLAP_LINK_OPTIONS -static)
            if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                list(APPEND CLAP_LINK_OPTIONS -static-libgcc -static-libstdc++)
            endif ()
            if (CONFIG_RENDERER_OPENGL)
                set(CLAP_EXECUTABLE_LINK_OPTIONS "")
                if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                    list(APPEND CLAP_EXECUTABLE_LINK_OPTIONS -static-libgcc -static-libstdc++)
                endif ()
            else ()
                set(CLAP_EXECUTABLE_LINK_OPTIONS ${CLAP_LINK_OPTIONS})
            endif ()
        else ()
            set(CLAP_LINK_OPTIONS "")
            set(CLAP_EXECUTABLE_LINK_OPTIONS "")
        endif ()
    endif ()
endif ()

enable_testing()

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "")
set(MESHOPT_BUILD_TOOLS OFF CACHE BOOL "")
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "")
add_subdirectory(deps/src/meshoptimizer EXCLUDE_FROM_ALL)
set_property(TARGET meshoptimizer PROPERTY FOLDER "ThirdPartyLibraries")

set(ODE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/ode")

if (WIN32)
    if (MINGW)
        set(ODE_LIBRARY "${ODE_ROOT}/lib/libode_doubles${W32LIBSUFFIX}.a")
    else ()
        set(ODE_LIBRARY "${ODE_ROOT}/lib/ode_doubles${W32LIBSUFFIX}.lib")
    endif ()
else ()
set(ODE_LIBRARY "${ODE_ROOT}/lib/libode.a")
endif ()

include(ExternalProject)
ExternalProject_Add(ode
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/ode
    CMAKE_ARGS
        -DCMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL}
        -DCMAKE_PROGRAM_PATH=${CMAKE_PROGRAM_PATH}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}
        -DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}
        -DCMAKE_C_COMPILER_FRONTEND_VARIANT=MSVC -DCMAKE_CXX_COMPILER_FRONTEND_VARIANT=MSVC
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_AR=${CMAKE_AR}
        -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
        -DCMAKE_CXX_FLAGS_DEBUG=${CMAKE_CXX_FLAGS_DEBUG}
        -DCMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}
        -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
        -DCMAKE_C_FLAGS_DEBUG=${CMAKE_C_FLAGS_DEBUG}
        -DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/ode
        -DODE_WITH_DEMOS=OFF -DODE_WITH_GIMPACT=OFF -DODE_WITH_TESTS=OFF
        -DODE_WITH_LIBCCD=ON -DODE_WITH_OPCODE=ON -DBUILD_SHARED_LIBS=OFF
        -DODE_WITH_TESTS=OFF
        -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
    USES_TERMINAL_BUILD TRUE
    BUILD_BYPRODUCTS
        ${ODE_LIBRARY}
    INSTALL_COMMAND
        cmake --build . --target install)

add_library(libode STATIC IMPORTED)
add_dependencies(libode ode)
set_property(TARGET libode PROPERTY IMPORTED_LOCATION "${ODE_LIBRARY}")
set(ODE_INCLUDE "${ODE_ROOT}/include")

add_subdirectory(core)
add_subdirectory(demo/ldjam56)
add_subdirectory(demo/ldjam57)

if (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    ExternalProject_Add(compat-host
        SOURCE_DIR  ${CMAKE_SOURCE_DIR}/compat
        BINARY_DIR  ${CMAKE_BINARY_DIR}/compat-host
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        INSTALL_COMMAND   ""
    )

    set(UCPIO_HOST_BUILD_DIR "${CMAKE_BINARY_DIR}/tools/ucpio")

    ExternalProject_Add(ucpio_host
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/tools/ucpio"
        BINARY_DIR "${UCPIO_HOST_BUILD_DIR}"
        DEPENDS compat-host cerrs_gen
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${UCPIO_HOST_BUILD_DIR}/install
            -DENGINE_SRC_DIR=${CMAKE_SOURCE_DIR}/core
            -Dclap_compat_DIR=${CMAKE_BINARY_DIR}/compat/cmake
            -Dclap_BINARY_DIR=${CMAKE_BINARY_DIR}
        BUILD_BYPRODUCTS "${UCPIO_HOST_BUILD_DIR}/ucpio"
        USES_TERMINAL_BUILD TRUE
        STEP_TARGETS build
    )
    add_subdirectory(tools/server)
endif ()
