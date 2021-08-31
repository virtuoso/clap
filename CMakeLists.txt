cmake_minimum_required(VERSION 3.7)
project(clap)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CARGO_RELEASE_FLAG "" CACHE INTERNAL "")
    set(TARGET_DIR "debug" CACHE INTERNAL "")
    set(CMAKE_C_FLAGS "-ggdb -fsanitize=address -fno-omit-frame-pointer")
else ()
    set(CARGO_RELEASE_FLAG "--release" CACHE INTERNAL "")
    set(TARGET_DIR "release" CACHE INTERNAL "")
endif ()

include(scripts/rust-wasm.cmake)

enable_testing()
#add_subdirectory(rust)

message("### configuration types: ${CMAKE_CONFIGURATION_TYPES}")
message("### build type: ${CMAKE_BUILD_TYPE}")
include(ExternalProject)
ExternalProject_Add(ode
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/ode
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_CONFIGURATION_TYPES=${CMAKE_CONFIGURATION_TYPES}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/ode
        -DODE_WITH_DEMOS=OFF -DODE_WITH_GIMPACT=OFF
        -DODE_WITH_LIBCCD=ON -DODE_WITH_OPCODE=ON -DBUILD_SHARED_LIBS=OFF
    INSTALL_COMMAND
        cmake --build . --target install)
# ExternalProject_Get_Property(ode install_dir)
# message("### ode directory = ${install_dir}")
set(ODE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/ode")
set(ODE_LIBRARY "${ODE_ROOT}/lib/libode.a")
set(ODE_INCLUDE "${ODE_ROOT}/include")

# add_subdirectory(ode)
add_subdirectory(core)
# find_package(PkgConfig REQUIRED)
# pkg_check_modules(ode NAMES "ode/")
