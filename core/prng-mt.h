/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PRNG_MT_H__
#define __CLAP_PRNG_MT_H__

/*
 * Internal shim between prng.c and the C++ std::mt19937_64 engine
 * (see prng-mt.cpp). prng.c allocates the state storage; the shim
 * constructs into and draws from it.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

size_t prng_mt19937_size(void);
size_t prng_mt19937_align(void);
void prng_mt19937_init(void *state, uint64_t seed);
uint64_t prng_mt19937_next(void *state);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __CLAP_PRNG_MT_H__ */
