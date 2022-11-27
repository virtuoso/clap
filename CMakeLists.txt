cmake_minimum_required(VERSION 3.7)
project(clap)

option(CLAP_BUILD_WITH_GLES BOOL OFF)
option(CLAP_BUILD_FINAL BOOL OFF)
option(CLAP_BUILD_NETWORKING BOOL OFF)
set(CLAP_SERVER_IP "127.0.0.1" CACHE STRING "Server IP address")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CARGO_RELEASE_FLAG "" CACHE INTERNAL "")
    set(TARGET_DIR "debug" CACHE INTERNAL "")
    set(CMAKE_C_FLAGS "-ggdb -fsanitize=address -fno-omit-frame-pointer -DCLAP_DEBUG")
else ()
    set(CARGO_RELEASE_FLAG "--release" CACHE INTERNAL "")
    set(TARGET_DIR "release" CACHE INTERNAL "")
endif ()

include(scripts/bootstrap.cmake)
bootstrap_deps("bootstrap.json")
if (WIN32)
    bootstrap_deps("bootstrap-win32.json")
endif ()

include(scripts/rust-wasm.cmake)

enable_testing()
#add_subdirectory(rust)

message("### configuration types: ${CMAKE_CONFIGURATION_TYPES}")
message("### build type: ${CMAKE_BUILD_TYPE}")

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "")
set(MESHOPT_BUILD_TOOLS OFF CACHE BOOL "")
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "")
add_subdirectory(deps/src/meshoptimizer)

if (WIN32)
    set(BUILD_TESTING OFF)
    add_subdirectory(deps/src/windows-libc)
    set_property(TARGET wlibc PROPERTY FOLDER "ThirdPartyLibraries")
endif ()

set_property(TARGET meshoptimizer PROPERTY FOLDER "ThirdPartyLibraries")

include(ExternalProject)
ExternalProject_Add(ode
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/ode
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_CONFIGURATION_TYPES=${CMAKE_CONFIGURATION_TYPES}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/ode
        -DODE_WITH_DEMOS=OFF -DODE_WITH_GIMPACT=OFF -DODE_WITH_TESTS=OFF
        -DODE_WITH_LIBCCD=ON -DODE_WITH_OPCODE=ON -DBUILD_SHARED_LIBS=OFF
    INSTALL_COMMAND
        cmake --build . --target install)
set(ODE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/ode")
set(ODE_LIBRARY "${ODE_ROOT}/lib/libode.a")
set(ODE_INCLUDE "${ODE_ROOT}/include")

# add_subdirectory(ode)
add_subdirectory(core)
# add_subdirectory(demo/ldjam52)
add_subdirectory(demo/ldjam56)

if (NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
    add_subdirectory(tools/server)
endif ()
# find_package(PkgConfig REQUIRED)
# pkg_check_modules(ode NAMES "ode/")
