# Generate a cpio archive with assets and an assembly file that includes it
function(asset_pack asset_dir asset)
    set(asset_asm ${asset}.S)
    set(asset_cpio ${asset}.cpio)
    file(GLOB_RECURSE assets ${asset_dir}/asset/*)
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
