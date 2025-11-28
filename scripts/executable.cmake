# Produce a clap executable for any supported platform (what used do be demo/*/CMakeLists.txt)
# @executable_name: cmake target name, also an executable name on most platforms
# @sources:         list of source files
# @ASSET_DIR:       asset source directory
# @assets:          list of assets @executable_name depends on and JSON files that reference
#                   other assets (GLTF files, SFX files)
# @font_file:       font file name for the web build, relative to @ASSET_DIR
# @build_in_assets: TRUE to use asset packer to build assets into the final executable
#
# Web builds will install into a directory named @executable name with the actual compiler
# generated files named index.{html,js,wasm}. Windows builds will have .exe suffix.
# Web builds have assets automatically packed in, so @build_in_assets is ignored there.
function (clap_executable executable_name sources ASSET_DIR assets font_file build_in_assets)
    set(ENGINE_SRC "${CMAKE_SOURCE_DIR}/core")
    set(ENGINE_INCLUDE "${ENGINE_SRC}" "${clap_BINARY_DIR}/core")
    # set(ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/asset")

    get_directory_property(CONFIG_GLES
        DIRECTORY "${CMAKE_SOURCE_DIR}/core"
        DEFINITION CONFIG_GLES)
    get_directory_property(CIMGUI_DIR
        DIRECTORY "${CMAKE_SOURCE_DIR}/core"
        DEFINITION CIMGUI_DIR)

    include(${CMAKE_SOURCE_DIR}/scripts/pack-assets.cmake)

    if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
        # set(EMSDK_SHELL "${CMAKE_CURRENT_BINARY_DIR}/shell_clap.html")
        set(EMSDK_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/shell_clap.html")
        set(EXTRA_LIBRARIES     "--shell-file=${EMSDK_SHELL}"
                                "--preload-file=${ASSET_DIR}@/asset")

        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
        else ()
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
        endif ()
        set(CMAKE_EXECUTABLE_SUFFIX ".html")
    elseif (build_in_assets)
        asset_pack(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/asset)
        set(ENGINE_ASSETS ${ASSET_FILE})
    endif ()

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        if (NOT MINGW)
            set(DEBUG_LIBRARIES ${ASAN_FLAG};${UBSAN_FLAG})
        endif ()
        if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
            set(DEBUG_LIBRARIES ${DEBUG_LIBRARIES} -g3 -gsource-map)
        endif ()
    else ()
        set(DEBUG_LIBRARIES "")
    endif ()

    set(ENGINE_MAIN ${sources})
    set(ENGINE_LIB libonehandclap)

    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(MACOSX_BUNDLE ON)
        add_executable(${executable_name} MACOSX_BUNDLE ${ENGINE_MAIN} ${ENGINE_ASSETS})

        set_target_properties(${executable_name} PROPERTIES
            MACOSX_BUNDLE ON
            MACOSX_BUNDLE_GUI_IDENTIFIER works.ash.clap.${executable_name}
            MACOSX_BUNDLE_BUNDLE_NAME ${executable_name}
            MACOSX_BUNDLE_BUNDLE_VERSION "${GIT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${GIT_SHORT_VERSION}"
        )
    else ()
        add_executable(${executable_name} ${ENGINE_MAIN} ${ENGINE_ASSETS})
    endif()

    add_dependencies(${executable_name} ${ENGINE_LIB} meshoptimizer)
    target_include_directories(${executable_name} PRIVATE ${ENGINE_INCLUDE} ${ODE_INCLUDE} ${CIMGUI_DIR})
    set_target_properties(${executable_name} PROPERTIES COMPILE_FLAGS "-Wall -Wno-misleading-indentation -Wno-comment -Werror")
    set_target_properties(${executable_name} PROPERTIES LINK_DEPENDS "${ASSETS};${EMSDK_SHELL}")
    target_link_libraries(${executable_name} PRIVATE ${FREETYPE_LIBRARIES} glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})
    target_link_libraries(${executable_name} PRIVATE ${PNG_LIBRARY})
    target_link_libraries(${executable_name} PRIVATE libode meshoptimizer)
    target_link_libraries(${executable_name} PRIVATE ${EXTRA_LIBRARIES})
    target_link_libraries(${executable_name} PRIVATE ${ENGINE_LIB})
    target_link_options(${executable_name} PRIVATE ${DEBUG_LIBRARIES})
    win32_executable(${executable_name})

    if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
        set_target_properties(${executable_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            OUTPUT_NAME index
            SUFFIX .html
        )

        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            install(TARGETS ${executable_name} DESTINATION ${executable_name}dbg)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm.map DESTINATION ${executable_name}dbg)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION ${executable_name}dbg)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION ${executable_name}dbg)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION ${executable_name}dbg)
            install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/background.jpg DESTINATION ${executable_name}dbg)
            install(FILES ${ASSET_DIR}/${font_file} DESTINATION ${executable_name}dbg)
        else ()
            if (CLAP_BUILD_FINAL)
                install(TARGETS ${executable_name} DESTINATION ${executable_name})
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION ${executable_name})
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION ${executable_name})
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION ${executable_name})
                install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/background.jpg DESTINATION ${executable_name})
                install(FILES ${ASSET_DIR}/${font_file} DESTINATION ${executable_name})
            else ()
                install(TARGETS ${executable_name} DESTINATION ${executable_name}test)
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION ${executable_name}test)
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION ${executable_name}test)
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION ${executable_name}test)
                install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/background.jpg DESTINATION ${executable_name}test)
                install(FILES ${ASSET_DIR}/${font_file} DESTINATION ${executable_name}test)
            endif ()
        endif ()
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        install(TARGETS ${executable_name} BUNDLE DESTINATION . COMPONENT ${executable_name})
        if (NOT ${CLAP_MACOS_DEV_TEAM_ID} STREQUAL "")
            install(CODE
                    "execute_process(
                        COMMAND
                            codesign
                                --deep
                                --force
                                --options runtime
                                --timestamp
                                -s \"${CLAP_MACOS_DEV_TEAM_ID}\"
                                \"${CMAKE_INSTALL_PREFIX}/${executable_name}.app\"
                    )"
            )
        endif ()
    else ()
        install(TARGETS ${executable_name} DESTINATION bin)
    endif ()
endfunction ()
