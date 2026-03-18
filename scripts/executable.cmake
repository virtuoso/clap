# Extract asset file names from a scene JSON file (GLTF models, SFX sound files)
# @asset_dir:       asset source directory
# @asset_build_dir: asset staging directory
# @scene_file:      scene JSON
# Sets ASSETS_FULL list variable in its parent scope
function (assets_install_from_scene asset_dir asset_build_dir scene_file)
    file(READ "${asset_dir}/${scene_file}" json)
    string(JSON nr_models LENGTH "${json}" "model")
    math(EXPR nr_models "${nr_models} - 1")

    set(asset_build_dir "${CMAKE_CURRENT_BINARY_DIR}/asset")
    file(MAKE_DIRECTORY ${asset_build_dir})

    # models
    foreach(idx RANGE ${nr_models})
        # GLTFs
        string(JSON gltf_name GET "${json}" "model" ${idx} "gltf")
        list(APPEND ASSETS_FULL "${gltf_name}")

        # model::sfx
        string(JSON nr_sfx ERROR_VARIABLE SFX_ERROR LENGTH "${json}" "model" ${idx} "sfx")
        if (NOT SFX_ERROR)
            math(EXPR nr_sfx "${nr_sfx} - 1")
            foreach (sfx_idx RANGE ${nr_sfx})
                string(JSON sfx_key MEMBER "${json}" "model" ${idx} "sfx" ${sfx_idx})
                string(JSON sfx_name GET "${json}" "model" ${idx} "sfx" "${sfx_key}")
                list(APPEND ASSETS_FULL "${sfx_name}")
            endforeach ()
        endif ()
    endforeach()

    # scene sfx
    string(JSON nr_sfx ERROR_VARIABLE SFX_ERROR LENGTH "${json}" "sfx")
    if (NOT SFX_ERROR)
        math(EXPR nr_sfx "${nr_sfx} - 1")
        foreach (sfx_idx RANGE ${nr_sfx})
            string(JSON sfx_key MEMBER "${json}" "sfx" ${sfx_idx})
            string(JSON sfx_name GET "${json}" "sfx" "${sfx_key}")
            list(APPEND ASSETS_FULL "${sfx_name}")
        endforeach ()
    endif ()
    set(ASSETS_FULL "${ASSETS_FULL}" PARENT_SCOPE)
endfunction ()

# Generate a command for each asset to be delivered to a staging directory, from which they can
# be packed into a cpio archive or a preload archive for wasm build
# @asset_dir:       asset source directory
# @asset_build_dir: asset staging directory
# @assets:          list of assets
function (assets_install asset_dir asset_build_dir assets)
    foreach (asset ${assets})
        add_custom_command(
            COMMENT "Installing ${asset}"
            OUTPUT ${asset_build_dir}/${asset}
            DEPENDS ${asset_dir}/${asset}
            COMMAND
                ${CMAKE_COMMAND}
                    -E copy ${asset_dir}/${asset} ${asset_build_dir}/${asset}
        )
    endforeach ()
endfunction ()

include(${CMAKE_SOURCE_DIR}/scripts/executable-macos.cmake)

# Produce a clap executable for any supported platform (what used do be demo/*/CMakeLists.txt)
# @executable_name: cmake target name, also an executable name on most platforms
# @title:           executable title
# @sources:         list of source files
# @asset_dir:       asset source directory
# @assets:          list of assets @executable_name depends on and JSON files that reference
#                   other assets (GLTF files, SFX files)
# @background_file: background image file
# @font_file:       font file name for the web build, relative to @asset_dir
# @font_family:     font family
# @build_in_assets: TRUE to use asset packer to build assets into the final executable
#
# Web builds will install into a directory named @executable name with the actual compiler
# generated files named index.{html,js,wasm}. Windows builds will have .exe suffix.
# Web builds have assets automatically packed in, so @build_in_assets is ignored there.
function (clap_executable executable_name title sources asset_dir assets background_file font_file font_family build_in_assets)
    set(ENGINE_SRC "${CMAKE_SOURCE_DIR}/core")
    set(ENGINE_INCLUDE "${ENGINE_SRC}" "${clap_BINARY_DIR}/core")
    set(asset_build_dir "${CMAKE_CURRENT_BINARY_DIR}/asset")

    # Populate assets_full and assets_build_full
    foreach (asset ${assets};${font_file})
        if ("${asset}" MATCHES ".json$")
            assets_install_from_scene("${asset_dir}" "${asset_build_dir}" "${asset}")
            list(APPEND assets_full ${ASSETS_FULL})
            foreach (x ${ASSETS_FULL})
                list(APPEND assets_build_full "${asset_build_dir}/${x}")
            endforeach ()
        endif ()

        list(APPEND assets_full "${asset}")
        list(APPEND assets_build_full "${asset_build_dir}/${asset}")
    endforeach ()

    # Generate per-asset commands
    assets_install("${asset_dir}" "${asset_build_dir}" "${assets_full}")

    # A catch-all target for all staged assets
    add_custom_target(assets-build-full-${executable_name} DEPENDS ${assets_build_full})

    get_directory_property(CONFIG_GLES
        DIRECTORY "${ENGINE_SRC}"
        DEFINITION CONFIG_GLES)
    get_directory_property(CIMGUI_DIR
        DIRECTORY "${ENGINE_SRC}"
        DEFINITION CIMGUI_DIR)
    get_directory_property(CMAKE_C_FLAGS
        DIRECTORY "${ENGINE_SRC}"
        DEFINITION CMAKE_C_FLAGS)

    include(${CMAKE_SOURCE_DIR}/scripts/pack-assets.cmake)

    if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
        set(SHELL_PAGE_TITLE "${title}")
        set(SHELL_FONT_FAMILY "${font_family}")
        cmake_path(GET background_file FILENAME SHELL_BACKGROUND_IMAGE)
        cmake_path(GET font_file FILENAME SHELL_FONT_FILE)

        set(EMSDK_SHELL "${CMAKE_CURRENT_BINARY_DIR}/shell.html")
        configure_file("${ENGINE_SRC}/shell.html.in" "${EMSDK_SHELL}")

        set(EXTRA_LIBRARIES     "--shell-file=${EMSDK_SHELL}"
                                "--preload-file=${asset_build_dir}@/asset")

        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
        else ()
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
        endif ()
        set(CMAKE_EXECUTABLE_SUFFIX ".html")
    elseif (build_in_assets)
        asset_pack(${CMAKE_CURRENT_BINARY_DIR} ${asset_build_dir} "${assets_build_full}")
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
    target_include_directories(${executable_name} PRIVATE
        ${ENGINE_INCLUDE}
        ${ODE_INCLUDE}
        ${CIMGUI_DIR}
        ${CLAP_DEPENDENCY_INCLUDE_DIRS}
        ${CLAP_RENDERER_INCLUDE_DIRS}
    )
    set_target_properties(${executable_name} PROPERTIES COMPILE_FLAGS "-Wall -Wno-misleading-indentation -Wno-comment -Werror ${CMAKE_C_FLAGS}")
    set_target_properties(${executable_name} PROPERTIES COMPILE_DEFINITIONS "CLAP_EXECUTABLE_TITLE=\"${title}\";CLAP_FONT_FILE=\"${font_file}\"")
    set_target_properties(${executable_name} PROPERTIES LINK_DEPENDS "${ASSETS};${EMSDK_SHELL}")
    target_link_libraries(${executable_name} PRIVATE ${CLAP_DEPENDENCY_LIBRARIES})
    target_link_libraries(${executable_name} PRIVATE ${CLAP_RENDERER_LIBRARIES})
    target_link_libraries(${executable_name} PRIVATE libode meshoptimizer)
    target_link_libraries(${executable_name} PRIVATE ${EXTRA_LIBRARIES})
    target_link_libraries(${executable_name} PRIVATE ${ENGINE_LIB})
    target_link_options(${executable_name} PRIVATE ${CLAP_EXECUTABLE_LINK_OPTIONS} ${DEBUG_LIBRARIES})
    win32_executable(${executable_name})

    if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
        add_dependencies(${executable_name} assets-build-full-${executable_name})

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
            install(FILES ${background_file} DESTINATION ${executable_name}dbg)
            install(FILES ${asset_dir}/${font_file} DESTINATION ${executable_name}dbg)
        else ()
            if (CLAP_BUILD_FINAL)
                install(TARGETS ${executable_name} DESTINATION ${executable_name})
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION ${executable_name})
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION ${executable_name})
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION ${executable_name})
                install(FILES ${background_file} DESTINATION ${executable_name})
                install(FILES ${asset_dir}/${font_file} DESTINATION ${executable_name})
            else ()
                install(TARGETS ${executable_name} DESTINATION ${executable_name}test)
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.wasm DESTINATION ${executable_name}test)
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.data DESTINATION ${executable_name}test)
                install(FILES ${CMAKE_CURRENT_BINARY_DIR}/index.js DESTINATION ${executable_name}test)
                install(FILES ${background_file} DESTINATION ${executable_name}test)
                install(FILES ${asset_dir}/${font_file} DESTINATION ${executable_name}test)
            endif ()
        endif ()
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        install(TARGETS ${executable_name} BUNDLE DESTINATION . COMPONENT ${executable_name})
        clap_macos_install_bundle(${executable_name}
            "works.ash.clap.${executable_name}")
    else ()
        install(TARGETS ${executable_name} DESTINATION bin)
    endif ()
endfunction ()
