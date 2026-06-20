/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PRNG_H__
#define __CLAP_PRNG_H__

#include <stdint.h>
#include "error.h"
#include "util.h"

/**
 * enum prng_alg - pseudorandom number generator backends
 * @PRNG_DEFAULT:       resolves to PRNG_XOSHIRO256PP
 * @PRNG_XOSHIRO256PP:  xoshiro256++ (Blackman/Vigna): 32 bytes of state,
 *                      fast, the current-best small-state generator
 * @PRNG_SPLITMIX64:    splitmix64 (Vigna): 8 bytes of state, the canonical
 *                      stream-splitting building block, also a fine main
 *                      generator when state size matters
 * @PRNG_MT19937_64:    std::mt19937_64: heavy (~2.5 kB of state), but a
 *                      known quantity that external tooling (C++ stdlib,
 *                      Python, Numpy) also reproduces
 *
 * Every backend produces bit-identical output for a given seed on all
 * platforms, unlike the libc rand()/drand48() family.
 */
typedef enum prng_alg {
    PRNG_DEFAULT = 0,
    PRNG_XOSHIRO256PP,
    PRNG_SPLITMIX64,
    PRNG_MT19937_64,
} prng_alg;

typedef struct prng prng;
cresp_ret(prng);
DECLARE_CLEANUP(prng);

/**
 * prng_new() - create a pseudorandom stream with its own state
 * @alg:    backend algorithm
 * @seed:   seed; the same (@alg, @seed) pair always produces the same stream
 *
 * The stream owns its state: nothing it does depends on (or disturbs) the
 * libc PRNG or any other prng instance, so generators drawing from separate
 * streams compose reproducibly.
 *
 * Return: the new stream to be prng_free()d by the caller, or CERR_NOMEM /
 * CERR_INVALID_ARGUMENTS.
 */
cresp_check(prng) prng_new(prng_alg alg, uint64_t seed);

/**
 * prng_free() - free a pseudorandom stream
 * @p:  the stream, may be NULL
 */
void prng_free(prng *p);

/**
 * prng_u64() - draw 64 random bits
 * @p:  the stream
 *
 * Return: a uniformly distributed 64-bit value.
 */
uint64_t prng_u64(prng *p);

/**
 * prng_u32() - draw 32 random bits
 * @p:  the stream
 *
 * Return: a uniformly distributed 32-bit value (the high half of a 64-bit
 * draw, where the weaker generators put their best bits).
 */
uint32_t prng_u32(prng *p);

/**
 * prng_unit() - draw a float in [0, 1)
 * @p:  the stream
 *
 * Return: a uniformly distributed float in [0, 1).
 */
float prng_unit(prng *p);

/**
 * prng_unitd() - draw a double in [0, 1)
 * @p:  the stream
 *
 * Return: a uniformly distributed double in [0, 1).
 */
double prng_unitd(prng *p);

/**
 * prng_range() - draw an integer in [@lo, @hi)
 * @p:  the stream
 * @lo: lowest value, inclusive
 * @hi: highest value, exclusive
 *
 * Return: a uniformly distributed integer in [@lo, @hi), or
 * CERR_INVALID_ARGUMENTS when @hi <= @lo (an empty range has nothing to
 * draw).
 */
cres_check(int) prng_range(prng *p, int lo, int hi);

/**
 * prng_split() - derive an independent child stream
 * @parent: the stream to split off of
 *
 * The child uses the parent's algorithm and a seed drawn from the parent's
 * dedicated split stream: it depends only on the parent's seed and how many
 * times the parent has been split, not on how much either stream has drawn.
 * That way recursive generators (BSP subtrees etc) can hand each branch its
 * own stream and stay reproducible no matter what runs in between.
 *
 * Return: the child stream to be prng_free()d by the caller, or CERR_NOMEM.
 */
cresp_check(prng) prng_split(prng *parent);

#endif /* __CLAP_PRNG_H__ */
