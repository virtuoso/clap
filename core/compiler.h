/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_COMPILER_H__
#define __CLAP_COMPILER_H__

#define CU(x) __attribute__((cleanup(cleanup__ ## x)))

#define weak __attribute__((weak))
#ifdef __APPLE__
#define rodata __attribute__((section("__DATA_CONST,__const")))
#else
#define rodata __attribute__((section(".rodata")))
#endif /* __APPLE__ */
#define nonstring __attribute__((nonstring))
#ifndef __unused
#define __unused __attribute__((unused))
#endif /* __unused */
#define notrace __attribute__((no_instrument_function))
#define __noreturn __attribute__((noreturn))
#define __printf(x, y) __attribute__((__format__(__printf__, (x), (y))))

/* Compiler annotations for things that can't be NULL or return NULL */
#ifndef __nonnull_params
#define __nonnull_params(params) __attribute__((__nonnull__ params))
#endif /* __nonnull_params */
#ifndef __returns_nonnull
#define __returns_nonnull __attribute__((__returns_nonnull__))
#endif /* __returns_nonnull */

/**
 * define noubsan - disable undefined behavior sanitizer locally
 *
 * UBSAN has false positives, so it is sometimes beneficial to
 * be able to disable it for a function.
 */
#define noubsan __attribute__((no_sanitize("undefined")))

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

/* Multiplication overflows are by definition unlikely */
#define _mul_overflow(a, b, res) __builtin_mul_overflow((a), (b), (res))
#define mul_overflow(a, b, res) unlikely(__builtin_mul_overflow((a), (b), (res)))

#ifndef offsetof
#define offsetof(a, b) __builtin_offsetof(a, b)
#endif /* offsetof */

#ifndef unreachable
#define unreachable __builtin_unreachable
#endif /* unreachable */

#define barrier() asm volatile("" ::: "memory")

#endif /* __CLAP_COMPILER_H__ */
