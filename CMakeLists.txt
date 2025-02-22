cmake_minimum_required(VERSION 3.21)
project(clap)

enable_language(C)

include (FindPkgConfig)
include(CheckSymbolExists)

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
        set(CMAKE_C_FLAGS "-ggdb -fsanitize=address -fno-omit-frame-pointer -DCLAP_DEBUG")
        set(CMAKE_CXX_FLAGS "-ggdb -fsanitize=address -fno-omit-frame-pointer -DCLAP_DEBUG")
        set(W32LIBSUFFIX "")
    endif ()
else ()
    set(CARGO_RELEASE_FLAG "--release" CACHE INTERNAL "")
    set(TARGET_DIR "release" CACHE INTERNAL "")
endif ()

include(scripts/bootstrap.cmake)
bootstrap_deps("bootstrap.json")
if (WIN32)
    bootstrap_deps("bootstrap-win32.json")
endif ()

if (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    include (FindGLEW)
    enable_language(ASM)
    check_symbol_exists(fls "strings.h" HAVE_FLS)
    set(OpenGL_GL_PREFERENCE "LEGACY")
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
    find_package(glfw3 REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(PNG REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(OpenAL REQUIRED)
    if (WIN32)
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
    if (WIN32)
        set(EXTRA_LIBRARIES ntdll Netapi32)
    else ()
        set(EXTRA_LIBRARIES m pthread stdc++)
    endif ()
endif ()

enable_testing()

include(scripts/win32-helpers.cmake)

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "")
set(MESHOPT_BUILD_TOOLS OFF CACHE BOOL "")
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "")
add_subdirectory(deps/src/meshoptimizer EXCLUDE_FROM_ALL)
set_property(TARGET meshoptimizer PROPERTY FOLDER "ThirdPartyLibraries")

wlibc_setup(deps/src/windows-libc)

set(ODE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/ode")

if (WIN32)
set(ODE_LIBRARY "${ODE_ROOT}/lib/ode_doubles${W32LIBSUFFIX}.lib")
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

if (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    add_subdirectory(tools/ucpio)
    add_subdirectory(tools/server)
endif ()
