# Generate a cpio archive with assets and an assembly file that includes it
function(asset_pack asset_dir asset)
    set(asset_asm ${asset}.S)
    set(asset_cpio ${asset}.cpio)
    set(asset_list_file ${CMAKE_CURRENT_BINARY_DIR}/asset.txt)
    file(GLOB_RECURSE assets ${asset_dir}/asset/*)
    string(REPLACE ";" "\n" asset_list "${assets}")
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" asset_list "${asset_list}")
    file(WRITE ${asset_list_file} ${asset_list})

    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        # On Mac OS X, cpio doesn't generate proper cpio archives, but pax does
        add_custom_command(
            OUTPUT ${asset_cpio}
            DEPENDS ${assets}
            COMMAND cd ${asset_dir} && echo asset | pax -w -x bcpio > ${asset_cpio}
        )
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        # On linux, good old cpio does the job
        add_custom_command(
            OUTPUT ${asset_cpio}
            DEPENDS ${assets}
            COMMAND cd ${asset_dir} && find asset | cpio -o > ${asset_cpio}
        )
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        # On Windows, we roll our own
        add_custom_command(
            OUTPUT ${asset_cpio}
            DEPENDS ${assets} ucpio
            COMMAND cd ${asset_dir} && ${CMAKE_BINARY_DIR}/tools/ucpio/ucpio -o < ${asset_list_file} > ${asset_cpio}
        )
    endif ()

    add_custom_command(
        COMMENT "Generating asset source"
        OUTPUT ${asset_asm}
        DEPENDS ${asset_cpio}
        COMMAND
            ${CMAKE_COMMAND}
                -Dasset_asm=${asset_asm}
                -Dasset_cpio=${asset_cpio}
                -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
                -P ${CMAKE_SOURCE_DIR}/scripts/builtin-assets.cmake
    )

    set(ASSET_FILE ${asset_asm} PARENT_SCOPE)
endfunction()
