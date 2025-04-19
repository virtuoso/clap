cmake_minimum_required(VERSION 3.21)

project(ucpio C)

set(CMAKE_C_STANDARD 23)

set(clap_compat_DIR ${clap_BINARY_DIR}/compat-host/cmake)
find_package(clap_compat CONFIG REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(ENGINE_INCLUDE "${ENGINE_SRC_DIR}" "${clap_BINARY_DIR}/core")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_LIBRARIES "-fsanitize=address")
else ()
    set(DEBUG_LIBRARIES "")
endif ()

set(UCPIO_BIN ucpio)

if (NOT WIN32)
    set_source_files_properties(ucpio.c PROPERTIES
        COMPILE_FLAGS "-Wall -Wno-misleading-indentation -Wno-comment -Wno-reserved-id-macro -Werror ${CFLAGS}"
    )
endif ()

set(UCPIO_SRC ucpio.c ${ENGINE_SRC_DIR}/cpio.c ${ENGINE_SRC_DIR}/memory.c)
set(ENGINE_LIB libonehandclap)

add_executable(${UCPIO_BIN} ${UCPIO_SRC})
target_link_libraries(${UCPIO_BIN} PRIVATE clap_compat)
target_include_directories(${UCPIO_BIN} PRIVATE ${ENGINE_INCLUDE})

install(TARGETS ${UCPIO_BIN} DESTINATION bin)
