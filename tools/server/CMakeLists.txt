set(CMAKE_C_STANDARD 11)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

get_filename_component(PARENT_DIR ${clap_SOURCE_DIR} DIRECTORY)
set(ENGINE_INCLUDE "${PARENT_DIR}/clap/core" "${clap_BINARY_DIR}/core")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_LIBRARIES "-fsanitize=address")
else ()
    set(DEBUG_LIBRARIES "")
endif ()

set(SERVER_BIN server)

set(ENGINE_MAIN server.c)
set(ENGINE_LIB libonehandclap)

add_executable(${SERVER_BIN} ${ENGINE_MAIN})
target_include_directories(${SERVER_BIN} PRIVATE ${ENGINE_INCLUDE} ${ODE_INCLUDE})
target_link_libraries(${SERVER_BIN} PRIVATE ${ENGINE_LIB})
win32_executable(${SERVER_BIN})

install(TARGETS ${SERVER_BIN} DESTINATION bin)
