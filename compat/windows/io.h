/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_COMPAT_WINDOWS_IO_H__
#define __CLAP_COMPAT_WINDOWS_IO_H__

#include <io.h>
#include <fcntl.h>
#include <stdio.h>

static inline void compat_set_binary(FILE *f)
{
    setmode(fileno(f), O_BINARY);
}

#define FOPEN_RB "rb"
#define FOPEN_WB "wb"

#endif /* __CLAP_COMPAT_WINDOWS_IO_H__ */
