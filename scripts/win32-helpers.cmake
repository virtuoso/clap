set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(WIN32_HOST ON)
elseif (WIN32)
    set(WIN32_HOST OFF)
endif ()

function(win32_library target includes)
    target_link_libraries(${target} PRIVATE clap_compat)
endfunction ()

function(win32_executable target)
    target_link_libraries(${target} PRIVATE clap_compat)
    if (WIN32)
        target_link_options(${target} PRIVATE
            -static-libgcc
            -static-libstdc++
        )
        target_link_libraries(${target} PRIVATE
            "-Wl,--push-state,-Bstatic"
            winpthread
            "-Wl,--pop-state"
        )
    endif ()
endfunction ()
