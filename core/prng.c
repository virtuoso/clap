// SPDX-License-Identifier: Apache-2.0
#include "memory.h"
#include "prng.h"
#include "prng-mt.h"

/*
 * splitmix64 and xoshiro256++ are transcribed from Sebastiano Vigna's
 * public domain reference implementations (https://prng.di.unimi.it/);
 * the std::mt19937_64 backend lives behind prng-mt.h.
 */

static uint64_t splitmix64_next(uint64_t *state)
{
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;

    return z ^ (z >> 31);
}

static uint64_t rotl64(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro256pp_next(uint64_t s[4])
{
    uint64_t result = rotl64(s[0] + s[3], 23) + s[0];
    uint64_t t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl64(s[3], 45);

    return result;
}

/*
 * Salts the split stream state so that for PRNG_SPLITMIX64 it doesn't
 * coincide with the main stream. The value is arbitrary (splitmix64's
 * first mixing multiplier, reused).
 */
#define PRNG_SPLIT_SALT 0xbf58476d1ce4e5b9ULL

struct prng {
    uint64_t    split;      /* splitmix64 state that prng_split() draws child seeds from */
    union {
        uint64_t    xo[4];  /* xoshiro256++ */
        uint64_t    sm;     /* splitmix64 */
    } u;
    prng_alg    alg;
    /* PRNG_MT19937_64: the C++ engine state follows, see mt_state() */
};

static void *mt_state(struct prng *p)
{
    return (unsigned char *)p + round_up(sizeof(*p), prng_mt19937_align());
}

cresp(prng) prng_new(prng_alg alg, uint64_t seed)
{
    if (alg == PRNG_DEFAULT)        alg = PRNG_XOSHIRO256PP;

    if (alg != PRNG_XOSHIRO256PP && alg != PRNG_SPLITMIX64 && alg != PRNG_MT19937_64)
        return cresp_error(prng, CERR_INVALID_ARGUMENTS,
                           .fmt = "invalid algorithm %d", .arg0 = (void *)(intptr_t)alg);

    size_t size = sizeof(struct prng);
    if (alg == PRNG_MT19937_64)     size = round_up(size, prng_mt19937_align()) + prng_mt19937_size();

    struct prng *p = mem_alloc(size);
    if (!p)                         return cresp_error(prng, CERR_NOMEM);

    p->alg = alg;
    p->split = seed ^ PRNG_SPLIT_SALT;

    switch (alg) {
        case PRNG_XOSHIRO256PP:
            /* canonical seeding: fill the state from a splitmix64 stream */
            for (int i = 0; i < 4; i++)
                p->u.xo[i] = splitmix64_next(&seed);
            break;
        case PRNG_SPLITMIX64:
            p->u.sm = seed;
            break;
        case PRNG_MT19937_64:
            prng_mt19937_init(mt_state(p), seed);
            break;
        default:
            clap_unreachable();
    }

    return cresp_val(prng, p);
}

void prng_free(prng *p)
{
    mem_free(p);
}

DEFINE_CLEANUP(prng, if (*p) prng_free(*p))

uint64_t prng_u64(prng *p)
{
    switch (p->alg) {
        case PRNG_XOSHIRO256PP:
            return xoshiro256pp_next(p->u.xo);
        case PRNG_SPLITMIX64:
            return splitmix64_next(&p->u.sm);
        case PRNG_MT19937_64:
            return prng_mt19937_next(mt_state(p));
        default:
            clap_unreachable();
    }
}

uint32_t prng_u32(prng *p)
{
    return (uint32_t)(prng_u64(p) >> 32);
}

float prng_unit(prng *p)
{
    return (prng_u64(p) >> 40) * 0x1p-24f;
}

double prng_unitd(prng *p)
{
    return (prng_u64(p) >> 11) * 0x1p-53;
}

cres(int) prng_range(prng *p, int lo, int hi)
{
    if (hi <= lo)
        return cres_error(int, CERR_INVALID_ARGUMENTS,
                          .fmt = "empty range [%d, %d)",
                          .arg0 = (void *)(intptr_t)lo, .arg1 = (void *)(intptr_t)hi);

    /*
     * Modulo carries a bias of span / 2^64, immaterial for layout
     * decisions and identical on every platform. The sum is done in 64
     * bits: a span wider than INT_MAX doesn't fit an int, its result does
     */
    int64_t span = (int64_t)hi - lo;

    return cres_val(int, (int)(lo + (int64_t)(prng_u64(p) % (uint64_t)span)));
}

cresp(prng) prng_split(prng *parent)
{
    return prng_new(parent->alg, splitmix64_next(&parent->split));
}
