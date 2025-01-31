/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_LIBRARIAN_FILE_H__
#define __CLAP_LIBRARIAN_FILE_H__

#include <sys/types.h>
#include <limits.h>

struct builtin_file {
    const char  *name;
    const char  *contents;
    size_t      size;
};

#endif /* __CLAP_LIBRARIAN_FILE_H__ */
