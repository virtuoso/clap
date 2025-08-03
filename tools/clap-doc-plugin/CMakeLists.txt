cmake_minimum_required(VERSION 3.21)
project(clap-doc-plugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# Enable Clang headers and link libraries
include_directories(
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

link_directories(
    ${LLVM_LIBRARY_DIRS}
    ${CLANG_LIBRARY_DIRS}
)

set_source_files_properties(clap-doc-plugin.cpp PROPERTIES
                            COMPILE_FLAGS "-Wno-deprecated-anon-enum-enum-conversion -Wno-deprecated-declarations")
add_llvm_library(clapdoc
    MODULE clap-doc-plugin.cpp
    PLUGIN_TOOL clang
)

target_link_libraries(clapdoc
    PRIVATE
        clangAST clangFrontend clangLex clangBasic
)

set_target_properties(clapdoc PROPERTIES
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
)
