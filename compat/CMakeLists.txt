if(NOT DEFINED PROJECT_NAME)
    cmake_minimum_required(VERSION 3.21)
    project(clap_compat C)
endif()

add_library(clap_compat INTERFACE)

include(CheckSymbolExists)

check_symbol_exists(ffs "strings.h" HAVE_FFS)
check_symbol_exists(fls "strings.h" HAVE_FLS)

if(MINGW)
  list(APPEND CMAKE_REQUIRED_LIBRARIES winpthread)
endif()

include(CheckCSourceCompiles)

check_c_source_compiles("
  #include <time.h>
  #if defined(__has_include)
  # if __has_include(<pthread_time.h>)
  #  include <pthread_time.h>
  # endif
  #endif
  int main(void){
    struct timespec ts;
    return clock_gettime(0, &ts);  // compile+link probe
  }"
  HAVE_CLOCK_GETTIME
)

configure_file(compat_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/compat_config.h)

target_compile_definitions(clap_compat INTERFACE -D_GNU_SOURCE)

set(COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")

target_include_directories(clap_compat INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}
)

set(COMPAT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/compat.h")
target_compile_options(clap_compat INTERFACE "-include" ${COMPAT_HEADER})

if (MINGW)
    set(COMPAT_WINDOWS_SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/windows/init.c"
        "${CMAKE_CURRENT_LIST_DIR}/windows/fs.c"
        "${CMAKE_CURRENT_LIST_DIR}/windows/string.c"
        "${CMAKE_CURRENT_LIST_DIR}/windows/time.c"
    )
    string(JOIN ";" COMPAT_WINDOWS_SOURCES_STRING ${COMPAT_WINDOWS_SOURCES})
    target_sources(clap_compat INTERFACE ${COMPAT_WINDOWS_SOURCES})
endif()

# Create build-friendly config
set(clap_compat_EXPORT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cmake")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/clap_compatConfig.cmake.in"
    "${clap_compat_EXPORT_DIR}/clap_compatConfig.cmake"
    @ONLY
)
