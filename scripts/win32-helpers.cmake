set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

function(wlibc_setup wlibc_dir)
    if (WIN32)
        set(BUILD_TESTING OFF)
        set(ENABLE_DLFCN OFF CACHE BOOL "")
        set(ENABLE_LANGINFO OFF CACHE BOOL "")
        set(ENABLE_EXTENDED_ATTRIBUTES OFF CACHE BOOL "")
        set(ENABLE_ACCOUNTS OFF CACHE BOOL "")
        set(ENABLE_TERMIOS OFF CACHE BOOL "")
        set(ENABLE_WCHAR_EXT OFF CACHE BOOL "")
        add_subdirectory(${wlibc_dir})
        set_property(TARGET wlibc PROPERTY FOLDER "ThirdPartyLibraries")
        set(WLIBC_LIBRARY "${CMAKE_BINARY_DIR}/wlibc${W32LIBSUFFIX}.lib" CACHE INTERNAL "WLIBC_LIBRARY")
        set(WLIBC_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/${wlibc_dir}/include" CACHE INTERNAL "WLIBC_INCLUDE_DIR")
        set(WLIBC_DEFINES "__forceinline=static\ inline\ __attribute__((always_inline))" CACHE INTERNAL "WLIBC_DEFINES")

        # hack a wlibc-min.lib
        file(GLOB
            wlibcmin_SOURCES
            "${wlibc_dir}/src/internal/wmain.c"
            # "${wlibc_dir}/src/internal/*.c"
            # "${wlibc_dir}/src/fcntl/internal.c"
        )
        set_source_files_properties(${wlibcmin_SOURCES} PROPERTIES
                                    COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;UMDF_USING_NTSTATUS;_CRT_SECURE_NO_WARNINGS;_CRT_NONSTDC_NO_DEPRECATE"
                                    COMPILE_OPTIONS "/Zi;/DEBUG;-UCLAP_DEBUG")
        add_library(wlibc-min ${wlibcmin_SOURCES})
        target_include_directories(wlibc-min PRIVATE ${WLIBC_INCLUDE_DIR})
    endif ()
endfunction ()

function(win32_library target includes)
    if (WIN32)
        target_compile_definitions(${target} PUBLIC ${WLIBC_DEFINES})
        if (includes)
            target_include_directories(${target} PUBLIC ${WLIBC_INCLUDE_DIR})
        endif ()
        target_link_libraries(${target} PRIVATE ${WLIBC_LIBRARY}) # internal
    endif ()
endfunction ()

function(win32_executable target)
    if (WIN32)
        # target_link_libraries(${target} PRIVATE internal ntdll Netapi32 ucrtd.lib vcruntimed.lib libcmtd.lib kernel32.lib advapi32.lib user32.lib legacy_stdio_definitions.lib)
        target_link_libraries(${target} PRIVATE ntdll Netapi32)
        target_link_options(${target} PRIVATE /Map:tests.map /DEBUG /ENTRY:wmainCRTStartup /SUBSYSTEM:CONSOLE)
        # if (NOT (${target} STREQUAL "test2"))
        #     message("### win32 target '${target}'")
        win32_library(${target} TRUE)
        # else ()
        #     target_link_libraries(${target} PRIVATE "${CMAKE_BINARY_DIR}/wlibc-min.lib")
        # endif ()
    endif ()
endfunction ()
