function(win32_fixup_flags)
    if (WIN32)
        set(CompilerFlags
                CMAKE_CXX_FLAGS
                CMAKE_CXX_FLAGS_DEBUG
                CMAKE_CXX_FLAGS_RELEASE
                CMAKE_C_FLAGS
                CMAKE_C_FLAGS_DEBUG
                CMAKE_C_FLAGS_RELEASE
                )
        foreach(CompilerFlag ${CompilerFlags})
            string(REPLACE "/MD${W32LIBSUFFIX}" "/MT${W32LIBSUFFIX}" ${CompilerFlag} "${${CompilerFlag}}")
            set(${CompilerFlag} ${${CompilerFlag}} CACHE INTERNAL "")
        endforeach()
    endif ()
endfunction ()

function(wlibc_setup wlibc_dir)
    if (WIN32)
        set(BUILD_TESTING OFF)
        add_subdirectory(${wlibc_dir})
        set_property(TARGET wlibc PROPERTY FOLDER "ThirdPartyLibraries")
        set(WLIBC_LIBRARY "${CMAKE_BINARY_DIR}/wlibc${W32LIBSUFFIX}.lib" CACHE INTERNAL "WLIBC_LIBRARY")
        set(WLIBC_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/${wlibc_dir}/include" CACHE INTERNAL "WLIBC_INCLUDE_DIR")
        set(WLIBC_DEFINES "__forceinline=static\ inline\ __attribute__((always_inline))" CACHE INTERNAL "WLIBC_DEFINES")
    endif ()
endfunction ()

function(win32_library target includes)
    if (WIN32)
        target_compile_definitions(${target} PUBLIC ${WLIBC_DEFINES})
        if (includes)
            target_include_directories(${target} PUBLIC ${WLIBC_INCLUDE_DIR})
        endif ()
        target_link_libraries(${target} PRIVATE ${WLIBC_LIBRARY})
    endif ()
endfunction ()

function(win32_executable target)
    if (WIN32)
        win32_library(${target} TRUE)
        target_link_libraries(${target} PRIVATE internal ntdll Netapi32)
        target_link_options(${target} PRIVATE /ENTRY:wmainCRTStartup /SUBSYSTEM:CONSOLE)
    endif ()
endfunction ()
