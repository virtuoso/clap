add_library(clap_compat INTERFACE)

include(CheckSymbolExists)

check_symbol_exists(ffs "strings.h" HAVE_FFS)
check_symbol_exists(fls "strings.h" HAVE_FLS)

configure_file(compat_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/compat_config.h)

target_compile_definitions(clap_compat INTERFACE -D_GNU_SOURCE)

set(COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")

target_include_directories(clap_compat INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}
)

set(COMPAT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/compat.h")
target_compile_options(clap_compat INTERFACE "-include" ${COMPAT_HEADER})
