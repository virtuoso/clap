/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_COMPAT_WINDOWS_PATH_H__
#define __CLAP_COMPAT_WINDOWS_PATH_H__

#include <ctype.h>

static inline void path_to_clap(char *s)
{
    for (; *s; s++)
        if (*s == '\\') *s = '/';
}

static inline void path_to_os(char *s)
{
    for (; *s; s++)
        if (*s == '/') *s = '\\';
}

#define PATH_DELIM_OS '\\'

static inline int path_cmp(const char *os_path, const char *clap_path)
{
    // On Windows, normalize slashes *and* use case-insensitive compare
    // You might use _stricmp if you're going full Win32
    while (*os_path && *clap_path) {
        char os_char = *os_path == PATH_DELIM_OS ? '/' : *os_path;
        if (tolower(os_char) != tolower(*clap_path))
            return (unsigned char)os_char - (unsigned char)*clap_path;

        ++os_path;
        ++clap_path;
    }
    return *os_path - *clap_path;
}

#endif /* __CLAP_COMPAT_SHARED_PATH_H__ */
