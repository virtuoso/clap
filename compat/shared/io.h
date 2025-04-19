/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_COMPAT_SHARED_IO_H__
#define __CLAP_COMPAT_SHARED_IO_H__

#include <stdio.h>

static inline void compat_set_binary(FILE *f) {}

#define FOPEN_RB "r"
#define FOPEN_WB "w"

#endif /* __CLAP_COMPAT_SHARED_IO_H__ */
