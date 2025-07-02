cmake_minimum_required(VERSION 3.21)
project(clap)

enable_language(C)

include (FindPkgConfig)

option(CLAP_BUILD_WITH_GLES BOOL OFF)
option(CLAP_BUILD_FINAL BOOL OFF)
option(CLAP_BUILD_NETWORKING BOOL OFF)
set(CLAP_SERVER_IP "127.0.0.1" CACHE STRING "Server IP address")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CARGO_RELEASE_FLAG "" CACHE INTERNAL "")
    set(TARGET_DIR "debug" CACHE INTERNAL "")
    if (WIN32)
        set(CMAKE_C_FLAGS "-DCLAP_DEBUG") # -fsanitize=address /Oy-
        set(CMAKE_CXX_FLAGS "-DCLAP_DEBUG")
        set(W32LIBSUFFIX "d")
    else ()
        set(CMAKE_C_FLAGS "-ggdb -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -DCLAP_DEBUG")
        set(CMAKE_CXX_FLAGS "-ggdb -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -DCLAP_DEBUG")
        set(W32LIBSUFFIX "")
    endif ()
else ()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_C_FLAGS "-flto=full")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_CXX_FLAGS "-flto=full")
    add_link_options("-flto=full")
    set(CARGO_RELEASE_FLAG "--release" CACHE INTERNAL "")
    set(TARGET_DIR "release" CACHE INTERNAL "")
endif ()

include(scripts/bootstrap.cmake)
bootstrap_deps("bootstrap.json")

add_subdirectory(${CMAKE_SOURCE_DIR}/compat)

include(scripts/win32-helpers.cmake)

if (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    include (FindGLEW)
    enable_language(ASM)
    set(OpenGL_GL_PREFERENCE "LEGACY")
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
    find_package(glfw3 REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(PNG REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(OpenAL REQUIRED)
    if (WIN32_HOST)
        # win32 doesn't have pkg-config
        find_package(Vorbis REQUIRED)
        set(VORBISFILE_LIBRARY Vorbis::vorbisfile)
    else ()
        # but debian derivatives don't supply cmake configs
        pkg_check_modules(VORBISFILE REQUIRED QUIET IMPORTED_TARGET vorbisfile)
        set(VORBISFILE_LIBRARY PkgConfig::VORBISFILE)
    endif ()

    set(PNG_LIBRARY PNG::PNG)
    set(OPENAL_LIBRARY OpenAL::OpenAL)
    set(GLEW_LIBRARIES GLEW::GLEW)
    if (WIN32 AND NOT MINGW)
        set(EXTRA_LIBRARIES ntdll Netapi32)
    elseif (NOT WIN32)
        set(EXTRA_LIBRARIES m pthread stdc++)
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
