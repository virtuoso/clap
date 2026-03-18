function (clap_macos_install_bundle executable_name bundle_identifier)
    set(clap_macos_install_script_path
        "${CMAKE_CURRENT_BINARY_DIR}/install-${executable_name}-macos.cmake")
    set(clap_macos_install_script_template [=[
set(clap_install_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
set(clap_app_path "${clap_install_root}/@executable_name@.app")
set(clap_dmg_path "${clap_install_root}/@executable_name@-@GIT_VERSION@.dmg")
set(clap_dmg_root "@CMAKE_CURRENT_BINARY_DIR@/@executable_name@-dmg-root")
set(clap_codesign_identity "@CLAP_MACOS_CODESIGN_IDENTITY@")
set(clap_notary_profile "@CLAP_MACOS_NOTARYTOOL_PROFILE@")
set(clap_dmg_identifier "@bundle_identifier@.dmg")

if (NOT EXISTS "${clap_app_path}")
    message(FATAL_ERROR "Missing app bundle for @executable_name@: ${clap_app_path}")
endif ()

if (NOT "${clap_codesign_identity}" STREQUAL "")
    execute_process(
        COMMAND
            /usr/bin/codesign
                --deep
                --force
                --options runtime
                --timestamp
                -s "${clap_codesign_identity}"
                "${clap_app_path}"
        RESULT_VARIABLE clap_result
    )
    if (NOT clap_result EQUAL 0)
        message(FATAL_ERROR "codesign failed for ${clap_app_path}")
    endif ()
endif ()

if (@CLAP_MACOS_CREATE_DMG@)
    file(REMOVE_RECURSE "${clap_dmg_root}")
    file(MAKE_DIRECTORY "${clap_dmg_root}")

    execute_process(
        COMMAND
            /usr/bin/ditto
                "${clap_app_path}"
                "${clap_dmg_root}/@executable_name@.app"
        RESULT_VARIABLE clap_result
    )
    if (NOT clap_result EQUAL 0)
        message(FATAL_ERROR "ditto failed while staging @executable_name@.app")
    endif ()

    execute_process(
        COMMAND
            /usr/bin/hdiutil
                create
                -fs HFS+
                -format UDZO
                -volname clap
                -srcfolder "${clap_dmg_root}"
                -ov
                "${clap_dmg_path}"
        RESULT_VARIABLE clap_result
    )
    if (NOT clap_result EQUAL 0)
        message(FATAL_ERROR "hdiutil failed while creating ${clap_dmg_path}")
    else ()
        file(REMOVE_RECURSE "${clap_app_path}")
    endif ()

    if (NOT "${clap_codesign_identity}" STREQUAL "")
        execute_process(
            COMMAND
                /usr/bin/codesign
                    --force
                    --timestamp
                    -i "${clap_dmg_identifier}"
                    -s "${clap_codesign_identity}"
                    "${clap_dmg_path}"
            RESULT_VARIABLE clap_result
        )
        if (NOT clap_result EQUAL 0)
            message(FATAL_ERROR "codesign failed for ${clap_dmg_path}")
        endif ()
    endif ()

    if (NOT "${clap_notary_profile}" STREQUAL "")
        if ("${clap_codesign_identity}" STREQUAL "")
            message(FATAL_ERROR
                "notarytool profile configured for @executable_name@ without a codesign identity")
        endif ()

        execute_process(
            COMMAND
                /usr/bin/xcrun
                    notarytool
                    submit
                    "${clap_dmg_path}"
                    --keychain-profile "${clap_notary_profile}"
                    --wait
                    --output-format json
            OUTPUT_VARIABLE clap_notary_output
            ERROR_VARIABLE clap_notary_error
            RESULT_VARIABLE clap_result
        )
        if (NOT clap_result EQUAL 0)
            message(FATAL_ERROR
                "notarytool submit failed for ${clap_dmg_path}; inspect notarytool history on the Apple account for details")
        endif ()

        string(JSON clap_notary_status ERROR_VARIABLE clap_notary_status_error
            GET "${clap_notary_output}" status)
        if (clap_notary_status_error)
            message(FATAL_ERROR "notarytool returned unreadable output for ${clap_dmg_path}")
        endif ()
        if (NOT "${clap_notary_status}" STREQUAL "Accepted")
            message(FATAL_ERROR "notarytool status for ${clap_dmg_path}: ${clap_notary_status}")
        endif ()

        execute_process(
            COMMAND
                /usr/bin/xcrun
                    stapler
                    staple
                    "${clap_dmg_path}"
            RESULT_VARIABLE clap_result
        )
        if (NOT clap_result EQUAL 0)
            message(FATAL_ERROR "stapler failed for ${clap_dmg_path}")
        endif ()
    endif ()

    file(REMOVE_RECURSE "${clap_dmg_root}")
elseif (NOT "@CLAP_MACOS_NOTARYTOOL_PROFILE@" STREQUAL "")
    message(FATAL_ERROR "CLAP_MACOS_NOTARYTOOL_PROFILE requires CLAP_MACOS_CREATE_DMG=ON")
endif ()
]=])
    string(CONFIGURE "${clap_macos_install_script_template}"
        clap_macos_install_script @ONLY)
    file(GENERATE OUTPUT "${clap_macos_install_script_path}"
        CONTENT "${clap_macos_install_script}")
    install(SCRIPT "${clap_macos_install_script_path}" COMPONENT ${executable_name})
endfunction ()
