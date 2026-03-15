include_guard(GLOBAL)

# Native non-Windows builds use pkg-config directly instead of find_package().
# For shipping builds we need the package's exact --libs --static and
# --cflags-only-I output so we can walk the dependency closure ourselves and
# replace non-system shared libraries with archives where possible. For normal
# Linux developer builds we preserve pkg-config's dynamic --libs output instead
# of forcing archive paths. find_package() is package-specific: some modules
# expose imported targets, others expose variables like FOO_LIBRARY,
# FOO_LIBRARIES, FOO_INCLUDE_DIR or FOO_INCLUDE_DIRS, and they do not
# consistently provide the full static transitive closure in one place. On
# Windows we still fall back to find_package() and probe the common variable
# spellings below.

# _clap_pkg_config() - run pkg-config and return stdout to the parent scope.
# @out_var: parent-scope variable that receives pkg-config stdout as a string.
# @ARGN: pkg-config arguments, for example --libs --static freetype2.
function(_clap_pkg_config out_var)
    if (CLAP_PKG_CONFIG_ENV_PATH)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env "PKG_CONFIG_PATH=${CLAP_PKG_CONFIG_ENV_PATH}" pkg-config ${ARGN}
            RESULT_VARIABLE pkg_res
            OUTPUT_VARIABLE pkg_out
            ERROR_VARIABLE pkg_err
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    else ()
        execute_process(
            COMMAND pkg-config ${ARGN}
            RESULT_VARIABLE pkg_res
            OUTPUT_VARIABLE pkg_out
            ERROR_VARIABLE pkg_err
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif ()
    if (NOT pkg_res EQUAL 0)
        string(STRIP "${pkg_err}" pkg_err)
        message(FATAL_ERROR "pkg-config ${ARGN} failed: ${pkg_err}")
    endif ()
    set(${out_var} "${pkg_out}" PARENT_SCOPE)
endfunction()

# _clap_is_system_path() - tell whether a library path belongs to the OS.
# @out_var: parent-scope variable that receives TRUE or FALSE.
# @path: absolute library or framework path to classify.
# System paths are left dynamic; non-system paths are candidates for static
# replacement on Apple and must be static on Linux.
function(_clap_is_system_path out_var path)
    set(is_system FALSE)

    if (IS_ABSOLUTE "${path}")
        if (APPLE)
            if ("${path}" MATCHES "^/System/Library/" OR
                "${path}" MATCHES "^/usr/lib/")
                set(is_system TRUE)
            endif ()
        elseif (UNIX)
            if ("${path}" MATCHES "^/lib/" OR
                "${path}" MATCHES "^/lib64/" OR
                "${path}" MATCHES "^/usr/lib/" OR
                "${path}" MATCHES "^/usr/lib64/")
                set(is_system TRUE)
            endif ()
        endif ()
    endif ()

    set(${out_var} "${is_system}" PARENT_SCOPE)
endfunction()

# _clap_find_library_file() - search one directory for a matching library file.
# @out_var: parent-scope variable that receives the first matching path or "".
# @dir: directory to search.
# @lib_name: logical library name without lib prefix or suffix, for example png16.
# @suffix: file suffix to match, typically .a or ${CMAKE_SHARED_LIBRARY_SUFFIX}.
function(_clap_find_library_file out_var dir lib_name suffix)
    set(candidate "")
    foreach(pattern
            "lib${lib_name}${suffix}"
            "${lib_name}${suffix}"
            "lib${lib_name}*${suffix}"
            "${lib_name}*${suffix}")
        file(GLOB matches LIST_DIRECTORIES FALSE "${dir}/${pattern}")
        if (matches)
            list(GET matches 0 candidate)
            break()
        endif ()
    endforeach()

    set(${out_var} "${candidate}" PARENT_SCOPE)
endfunction()

# _clap_collect_library_search_dirs() - merge explicit and implicit link dirs.
# @out_var: parent-scope variable that receives the deduplicated directory list.
# @ARGN: explicit search directories, typically collected from pkg-config -L.
# pkg-config often omits the platform's default multiarch directories from
# --libs --static output, so we also search the toolchain's implicit linker
# directories and CMake's library path hints.
function(_clap_collect_library_search_dirs out_var)
    set(all_dirs ${ARGN})

    foreach(var_name
            CMAKE_C_IMPLICIT_LINK_DIRECTORIES
            CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES
            CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES
            CMAKE_SYSTEM_LIBRARY_PATH
            CMAKE_LIBRARY_PATH)
        if (DEFINED ${var_name})
            list(APPEND all_dirs ${${var_name}})
        endif ()
    endforeach ()

    if (DEFINED ENV{LIBRARY_PATH} AND NOT "$ENV{LIBRARY_PATH}" STREQUAL "")
        string(REPLACE ":" ";" env_library_path "$ENV{LIBRARY_PATH}")
        list(APPEND all_dirs ${env_library_path})
    endif ()

    if (all_dirs)
        list(REMOVE_DUPLICATES all_dirs)
    endif ()

    foreach(search_dir ${all_dirs})
        if (IS_DIRECTORY "${search_dir}")
            list(APPEND existing_dirs "${search_dir}")
        endif ()
    endforeach ()

    if (existing_dirs)
        list(REMOVE_DUPLICATES existing_dirs)
    endif ()

    set(${out_var} "${existing_dirs}" PARENT_SCOPE)
endfunction()

# _clap_keep_linux_runtime_dynamic() - keep glibc/runtime libs dynamic.
# @out_var: parent-scope variable that receives TRUE or FALSE.
# @lib_name: logical library name without lib prefix or suffix.
# Linux ship builds that intentionally avoid the final -static still need a few
# libc-adjacent libraries to stay as normal -lfoo references; linking their
# archives directly into a dynamic executable triggers glibc IFUNC/runtime
# issues. Third-party dependencies still resolve to archives.
function(_clap_keep_linux_runtime_dynamic out_var lib_name)
    set(keep_dynamic FALSE)

    if (UNIX AND NOT APPLE AND CLAP_ALLOW_DYNAMIC_RUNTIME_LIBS)
        set(dynamic_runtime_libs
            c
            dl
            m
            pthread
            rt
            util
            resolv
            nsl
            anl
            gcc_s
        )
        list(FIND dynamic_runtime_libs "${lib_name}" keep_dynamic_idx)
        if (NOT keep_dynamic_idx EQUAL -1)
            set(keep_dynamic TRUE)
        endif ()
    endif ()

    set(${out_var} "${keep_dynamic}" PARENT_SCOPE)
endfunction()

# _clap_dynamicize_linux_runtime_libs() - convert runtime archive paths to -lfoo.
# @out_var: parent-scope variable that receives the rewritten link-item list.
# @ARGN: link items to rewrite.
# This is a final post-pass for Linux ship builds that keep the desktop OpenGL
# executable dynamic at the glibc/runtime boundary. It rewrites any absolute
# archive or shared-library path for a known runtime library back to its bare
# logical name so the linker can pick the dynamic form when -static is absent.
function(_clap_dynamicize_linux_runtime_libs out_var)
    foreach(item ${ARGN})
        if (IS_ABSOLUTE "${item}")
            get_filename_component(item_name "${item}" NAME)
            if ("${item_name}" MATCHES "^lib(.+)\\.[^.]+$")
                set(item_stem "${CMAKE_MATCH_1}")
                _clap_keep_linux_runtime_dynamic(keep_dynamic_runtime "${item_stem}")
                if (keep_dynamic_runtime)
                    list(APPEND rewritten_items "${item_stem}")
                    continue()
                endif ()
            endif ()
        endif ()

        list(APPEND rewritten_items "${item}")
    endforeach ()

    set(${out_var} "${rewritten_items}" PARENT_SCOPE)
endfunction()

# _clap_resolve_library_name() - resolve a -lfoo token to a concrete link item.
# @out_var: parent-scope variable that receives the resolved link item.
# @pkg_name: pkg-config package being processed, used for diagnostics.
# @lib_name: logical library name from a -l flag.
# @search_dirs: pkg-config -L directories to search in order.
# On macOS this prefers a non-system archive, otherwise keeps a non-system
# shared library path if no archive exists, otherwise falls back to the bare
# name for system libraries. On Linux ship builds prefer archives but only
# require them when CLAP_REQUIRE_STATIC_ARCHIVES is enabled; normal Linux
# builds preserve the original -lfoo semantics instead of forcing either
# archive or shared-library paths.
function(_clap_resolve_library_name out_var pkg_name lib_name search_dirs)
    set(static_path "")
    set(dynamic_path "")
    _clap_collect_library_search_dirs(effective_search_dirs ${search_dirs})

    _clap_keep_linux_runtime_dynamic(keep_dynamic_runtime "${lib_name}")
    if (keep_dynamic_runtime)
        set(${out_var} "${lib_name}" PARENT_SCOPE)
        return()
    endif ()

    foreach(search_dir ${effective_search_dirs})
        if (NOT static_path)
            _clap_find_library_file(candidate "${search_dir}" "${lib_name}" ".a")
            if (candidate)
                set(static_path "${candidate}")
            endif ()
        endif ()

        if (NOT dynamic_path)
            _clap_find_library_file(candidate "${search_dir}" "${lib_name}" "${CMAKE_SHARED_LIBRARY_SUFFIX}")
            if (candidate)
                set(dynamic_path "${candidate}")
            endif ()
        endif ()
    endforeach()

    if (APPLE)
        if (static_path)
            _clap_is_system_path(static_is_system "${static_path}")
            if (NOT static_is_system)
                set(${out_var} "${static_path}" PARENT_SCOPE)
                return()
            endif ()
        endif ()

        if (dynamic_path)
            _clap_is_system_path(dynamic_is_system "${dynamic_path}")
            if (NOT dynamic_is_system)
                message(STATUS "static-linkage: ${pkg_name}: no static archive for ${lib_name}, using ${dynamic_path}")
                set(${out_var} "${dynamic_path}" PARENT_SCOPE)
                return()
            endif ()
        endif ()

        set(${out_var} "${lib_name}" PARENT_SCOPE)
        return()
    endif ()

    if (UNIX AND NOT APPLE AND NOT CLAP_REQUIRE_STATIC_ARCHIVES)
        set(${out_var} "${lib_name}" PARENT_SCOPE)
        return()
    endif ()

    if (static_path)
        set(${out_var} "${static_path}" PARENT_SCOPE)
        return()
    endif ()

    if (CLAP_REQUIRE_STATIC_ARCHIVES)
        if (dynamic_path)
            message(FATAL_ERROR
                "static-linkage: ${pkg_name}: missing static archive for ${lib_name} (${dynamic_path})")
        endif ()

        string(JOIN ", " searched_dirs ${effective_search_dirs})
        message(FATAL_ERROR
            "static-linkage: ${pkg_name}: could not resolve a static archive for ${lib_name}; "
            "searched: ${searched_dirs}")
    endif ()

    if (dynamic_path)
        set(${out_var} "${dynamic_path}" PARENT_SCOPE)
    else ()
        set(${out_var} "${lib_name}" PARENT_SCOPE)
    endif ()
endfunction()

# _clap_resolve_absolute_library() - resolve an absolute library path token.
# @out_var: parent-scope variable that receives the resolved link item.
# @pkg_name: pkg-config package being processed, used for diagnostics.
# @lib_path: absolute path emitted by pkg-config.
# If a sibling static archive exists, prefer it under the same platform rules
# as _clap_resolve_library_name(). Otherwise keep the original absolute path,
# unless CLAP_REQUIRE_STATIC_ARCHIVES is enabled on Linux.
function(_clap_resolve_absolute_library out_var pkg_name lib_path)
    get_filename_component(lib_dir "${lib_path}" DIRECTORY)
    get_filename_component(lib_name "${lib_path}" NAME)

    if ("${lib_name}" MATCHES "^lib(.+)\\.[^.]+$")
        set(stem "${CMAKE_MATCH_1}")
    else ()
        set(${out_var} "${lib_path}" PARENT_SCOPE)
        return()
    endif ()

    _clap_keep_linux_runtime_dynamic(keep_dynamic_runtime "${stem}")
    if (keep_dynamic_runtime)
        set(${out_var} "${stem}" PARENT_SCOPE)
        return()
    endif ()

    _clap_find_library_file(static_path "${lib_dir}" "${stem}" ".a")
    if (APPLE AND static_path)
        _clap_is_system_path(static_is_system "${static_path}")
        if (NOT static_is_system)
            set(${out_var} "${static_path}" PARENT_SCOPE)
            return()
        endif ()
    elseif (UNIX AND NOT APPLE AND CLAP_REQUIRE_STATIC_ARCHIVES AND static_path)
        set(${out_var} "${static_path}" PARENT_SCOPE)
        return()
    endif ()

    if (UNIX AND NOT APPLE AND CLAP_REQUIRE_STATIC_ARCHIVES)
        message(FATAL_ERROR
            "static-linkage: ${pkg_name}: missing static archive next to ${lib_path}")
    endif ()

    set(${out_var} "${lib_path}" PARENT_SCOPE)
endfunction()

# _clap_resolve_pkg_config_libs() - expand pkg-config static libs into link items.
# @out_var: parent-scope variable that receives the resolved link-item list.
# @pkg_name: pkg-config package name, for example glfw3 or freetype2.
# This parses pkg-config --libs output for normal Linux developer builds and
# --libs --static elsewhere, keeps framework pairs intact, tracks -L search
# paths, resolves -lfoo tokens to archives or allowed shared paths, and
# preserves any other flags verbatim.
function(_clap_resolve_pkg_config_libs out_var pkg_name)
    if (UNIX AND NOT APPLE AND NOT CLAP_REQUIRE_STATIC_ARCHIVES)
        _clap_pkg_config(pkg_libs_raw --libs ${pkg_name})
    else ()
        _clap_pkg_config(pkg_libs_raw --libs --static ${pkg_name})
    endif ()
    separate_arguments(pkg_flags UNIX_COMMAND "${pkg_libs_raw}")

    set(search_dirs "")
    set(is_framework FALSE)

    foreach(flag ${pkg_flags})
        if (is_framework)
            list(APPEND resolved_libs "-framework ${flag}")
            set(is_framework FALSE)
        elseif ("${flag}" STREQUAL "-framework")
            set(is_framework TRUE)
        elseif ("${flag}" MATCHES "^-L(.+)$")
            list(APPEND search_dirs "${CMAKE_MATCH_1}")
            if (UNIX AND NOT APPLE AND NOT CLAP_REQUIRE_STATIC_ARCHIVES)
                list(APPEND resolved_libs "${flag}")
            endif ()
        elseif ("${flag}" MATCHES "^-l(.+)$")
            _clap_resolve_library_name(resolved "${pkg_name}" "${CMAKE_MATCH_1}" "${search_dirs}")
            list(APPEND resolved_libs "${resolved}")
        elseif (IS_ABSOLUTE "${flag}")
            _clap_resolve_absolute_library(resolved "${pkg_name}" "${flag}")
            list(APPEND resolved_libs "${resolved}")
        else ()
            list(APPEND resolved_libs "${flag}")
        endif ()
    endforeach ()

    set(${out_var} "${resolved_libs}" PARENT_SCOPE)
endfunction()

# _clap_resolve_pkg_config_includes() - collect include dirs from pkg-config.
# @out_var: parent-scope variable that receives the include-directory list.
# @pkg_name: pkg-config package name, for example glfw3 or freetype2.
# The result is deduplicated and contains only directories from -I flags.
function(_clap_resolve_pkg_config_includes out_var pkg_name)
    _clap_pkg_config(pkg_cflags_raw --cflags-only-I ${pkg_name})
    separate_arguments(pkg_cflags UNIX_COMMAND "${pkg_cflags_raw}")

    foreach(flag ${pkg_cflags})
        if ("${flag}" MATCHES "^-I(.+)$")
            list(APPEND include_dirs "${CMAKE_MATCH_1}")
        endif ()
    endforeach ()

    if (include_dirs)
        list(REMOVE_DUPLICATES include_dirs)
    endif ()

    set(${out_var} "${include_dirs}" PARENT_SCOPE)
endfunction()

# _clap_find_windows_imported_target() - prefer imported package targets on Windows.
# @out_var: parent-scope variable that receives the chosen imported target or "".
# @name: CMake package name passed to find_package(), for example glfw3 or PNG.
# @name_uc: upper-case variant of @name for common namespace spellings.
# vcpkg config packages often expose imported targets that carry the full static
# dependency closure and correct link order. Flattening their *_LIBRARIES
# variables can lose either the main archive itself or its transitive deps.
function(_clap_find_windows_imported_target out_var name name_uc)
    set(candidates
        "${name}::${name}"
        "${name}::${name_uc}"
        "${name_uc}::${name}"
        "${name_uc}::${name_uc}"
        "${name}"
        "${name_uc}")

    # The above covers most cases, but glfw3 -> glfw mapping is harder to
    # guess and we don't want implement elaborate heuristics for "guess how
    # this package calls its cmake targets". Thus, the quirk below.
    if ("${name}" STREQUAL "glfw3")
        list(PREPEND candidates glfw)
    endif ()

    foreach(candidate ${candidates})
        if (TARGET "${candidate}")
            set(${out_var} "${candidate}" PARENT_SCOPE)
            return()
        endif ()
    endforeach ()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# find_static_lib() - populate package include/library variables for callers.
# @name: CMake package name prefix to expose, for example Freetype or glfw3.
# @pkg_name: pkg-config package name used on non-Windows platforms.
# Sets the following variables in the parent scope:
#   ${name}_LIBRARIES
#   ${NAME_UC}_LIBRARIES
#   ${name}_INCLUDE_DIRS
#   ${NAME_UC}_INCLUDE_DIRS
# On non-Windows this comes from pkg-config and the resolver helpers above. On
# Windows this falls back to find_package() and probes the common variable
# spellings because package modules/configs are not uniform there either.
function(find_static_lib name pkg_name)
    string(TOUPPER "${name}" name_uc)

    if (WIN32)
        find_package(${name} REQUIRED)

        _clap_find_windows_imported_target(imported_target "${name}" "${name_uc}")
        if (imported_target)
            set(resolved_libs "${imported_target}")
        else ()
            foreach(var_name
                    ${name}_LIBRARIES
                    ${name}_LIBRARY
                    ${name_uc}_LIBRARIES
                    ${name_uc}_LIBRARY)
                if (DEFINED ${var_name} AND NOT "${${var_name}}" MATCHES ".*-NOTFOUND")
                    set(resolved_libs "${${var_name}}")
                    break()
                endif ()
            endforeach ()

            foreach(var_name
                    ${name}_INCLUDE_DIRS
                    ${name}_INCLUDE_DIR
                    ${name_uc}_INCLUDE_DIRS
                    ${name_uc}_INCLUDE_DIR)
                if (DEFINED ${var_name} AND NOT "${${var_name}}" MATCHES ".*-NOTFOUND")
                    set(include_dirs "${${var_name}}")
                    break()
                endif ()
            endforeach ()
        endif ()
    else ()
        _clap_resolve_pkg_config_libs(resolved_libs "${pkg_name}")
        _clap_resolve_pkg_config_includes(include_dirs "${pkg_name}")
    endif ()

    set(${name}_LIBRARIES "${resolved_libs}" PARENT_SCOPE)
    set(${name_uc}_LIBRARIES "${resolved_libs}" PARENT_SCOPE)
    set(${name}_INCLUDE_DIRS "${include_dirs}" PARENT_SCOPE)
    set(${name_uc}_INCLUDE_DIRS "${include_dirs}" PARENT_SCOPE)
endfunction()
