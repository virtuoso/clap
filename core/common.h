/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_COMMON_H__
#define __CLAP_COMMON_H__

#include "config.h"
#ifdef CONFIG_BROWSER
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#define EM_ASM(x)
#endif

#ifdef CONFIG_BROWSER
#define enter_debugger() emscripten_debugger()
#define PRItime "lld"
#define PRItvsec "lld"
#else
#include <stdlib.h>
#define enter_debugger() abort()
#ifdef _WIN32
#define PRItime "lld"
#define PRItvsec "lld"
#else
#define PRItime "ld"
#define PRItvsec "ld"
#endif /* _WIN32 */
#endif /* CONFIG_BROWSER */

#define BITS_PER_LONG   (8 * sizeof(long))
#define DINFINITY (__builtin_inf())

#if UINTPTR_MAX > ULONG_MAX
#define UINTPTR_SUFFIX ull
#define UINTPTR_CONST(x) (x ## ull)
#else
#define UINTPTR_SUFFIX ul
#define UINTPTR_CONST(x) (x ## ul)
#endif

#if defined(__has_feature)
# if __has_feature(address_sanitizer)
#define HAVE_ASAN 1
# endif /* __has_feature(address_sanitizer) */
#endif /* __has_feature */

#ifdef __SANITIZE_ADDRESS__
#define HAVE_ASAN 1
#endif /* __SANITIZE_ADDRESS__ */

#include "util.h"
#include "logger.h"
#include "clap.h"

#define bug_on(_c, args...) do { if ((_c)) { err("condition '" # _c "': " args); enter_debugger();} } while (0)

#endif /* __CLAP_COMMON_H__*/
