set(CMAKE_C_STANDARD 23)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(ENGINE_SRC_DIR "${CMAKE_SOURCE_DIR}/core")
set(ENGINE_INCLUDE "${ENGINE_SRC_DIR}" "${clap_BINARY_DIR}/core")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_LIBRARIES "-fsanitize=address")
else ()
    set(DEBUG_LIBRARIES "")
endif ()

set(UCPIO_BIN ucpio)

set(UCPIO_SRC ucpio.c ${ENGINE_SRC_DIR}/cpio.c ${ENGINE_SRC_DIR}/memory.c)
set(ENGINE_LIB libonehandclap)

add_executable(${UCPIO_BIN} ${UCPIO_SRC})
target_include_directories(${UCPIO_BIN} PRIVATE ${ENGINE_INCLUDE})
# target_link_libraries(${UCPIO_BIN} PRIVATE ${ENGINE_LIB})
win32_executable(${UCPIO_BIN})

install(TARGETS ${UCPIO_BIN} DESTINATION bin)
