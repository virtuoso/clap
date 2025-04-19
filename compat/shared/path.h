/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_COMPAT_SHARED_PATH_H__
#define __CLAP_COMPAT_SHARED_PATH_H__

#include <string.h>

static inline void path_to_clap(char *s) {}
static inline void path_to_os(char *s) {}

#define PATH_DELIM_OS '/'

static inline int path_cmp(const char *os_path, const char *clap_path)
{
    return strcmp(os_path, clap_path);
}

#endif /* __CLAP_COMPAT_SHARED_PATH_H__ */
