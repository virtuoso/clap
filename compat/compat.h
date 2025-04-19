/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_COMPAT_H__
#define __CLAP_COMPAT_H__

#ifndef __ASSEMBLER__

#include "compat_config.h"

#if defined(_WIN32)
# if !defined(WLIBC_H)
#  include "windows/compat.h"
# endif /* !WLIBC_H */
# include "windows/io.h"
# include "windows/path.h"
#else
# include "shared/io.h"
# include "shared/path.h"
#endif

#include "shared/strings.h"

#endif /* __ASSEMBLER__ */

#endif /* __CLAP_COMPAT_H__ */
