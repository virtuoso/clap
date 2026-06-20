// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <stdlib.h>
#include "prng.h"
#include "test.h"

static int prng_vectors_test(void)
{
    /*
     * Pinned reference outputs: a mismatch on any platform breaks the
     * bit-stability that seeded content relies on. The mt19937_64 values
     * double as an independent check of the C++ shim: the 10000th draw of
     * a default (5489) seeded engine is mandated by the C++ standard.
     */
    static const uint64_t xo42[] = {
        15021278609987233951ULL, 5881210131331364753ULL,
        18149643915985481100ULL, 12933668939759105464ULL,
    };
    static const uint64_t sm42[] = {
        13679457532755275413ULL, 2949826092126892291ULL,
        5139283748462763858ULL, 6349198060258255764ULL,
    };
    /* splitmix64's widely circulated reference vector */
    static const uint64_t sm1234567[] = {
        6457827717110365317ULL, 3203168211198807973ULL, 9817491932198370423ULL,
        4593380528125082431ULL, 16408922859458223821ULL,
    };
    static const uint64_t mt42[] = {
        13930160852258120406ULL, 11788048577503494824ULL, 13874630024467741450ULL,
    };

    LOCAL_SET(prng, xo) = CRES_RET(prng_new(PRNG_XOSHIRO256PP, 42), return EXIT_FAILURE);
    for (size_t i = 0; i < array_size(xo42); i++)
        if (prng_u64(xo) != xo42[i])            return EXIT_FAILURE;

    LOCAL_SET(prng, sm) = CRES_RET(prng_new(PRNG_SPLITMIX64, 42), return EXIT_FAILURE);
    for (size_t i = 0; i < array_size(sm42); i++)
        if (prng_u64(sm) != sm42[i])            return EXIT_FAILURE;

    LOCAL_SET(prng, smref) = CRES_RET(prng_new(PRNG_SPLITMIX64, 1234567), return EXIT_FAILURE);
    for (size_t i = 0; i < array_size(sm1234567); i++)
        if (prng_u64(smref) != sm1234567[i])    return EXIT_FAILURE;

    LOCAL_SET(prng, mt) = CRES_RET(prng_new(PRNG_MT19937_64, 42), return EXIT_FAILURE);
    for (size_t i = 0; i < array_size(mt42); i++)
        if (prng_u64(mt) != mt42[i])            return EXIT_FAILURE;

    LOCAL_SET(prng, mtdef) = CRES_RET(prng_new(PRNG_MT19937_64, 5489), return EXIT_FAILURE);
    for (int i = 0; i < 9999; i++)
        prng_u64(mtdef);
    if (prng_u64(mtdef) != 9981545732273789042ULL)  return EXIT_FAILURE;

    /* PRNG_DEFAULT is xoshiro256++ */
    LOCAL_SET(prng, def) = CRES_RET(prng_new(PRNG_DEFAULT, 42), return EXIT_FAILURE);
    if (prng_u64(def) != xo42[0])               return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int prng_split_test(void)
{
    /* first split child of a seed-42 xoshiro256++ stream, pinned */
    static const uint64_t child42[] = { 9556818641353097693ULL, 16322331957966639933ULL };

    LOCAL_SET(prng, p) = CRES_RET(prng_new(PRNG_DEFAULT, 42), return EXIT_FAILURE);
    LOCAL_SET(prng, c) = CRES_RET(prng_split(p), return EXIT_FAILURE);
    for (size_t i = 0; i < array_size(child42); i++)
        if (prng_u64(c) != child42[i])          return EXIT_FAILURE;

    /*
     * A child stream depends on the parent's seed and split count alone:
     * draws on the parent between splits don't change what the next
     * split produces
     */
    LOCAL_SET(prng, p2) = CRES_RET(prng_new(PRNG_DEFAULT, 42), return EXIT_FAILURE);
    for (int i = 0; i < 7; i++)
        prng_u64(p2);
    LOCAL_SET(prng, c2) = CRES_RET(prng_split(p2), return EXIT_FAILURE);
    if (prng_u64(c2) != child42[0])             return EXIT_FAILURE;

    /* successive splits produce different streams */
    LOCAL_SET(prng, c3) = CRES_RET(prng_split(p2), return EXIT_FAILURE);
    if (prng_u64(c3) == child42[0])             return EXIT_FAILURE;

    /* the child inherits the parent's algorithm */
    LOCAL_SET(prng, smp) = CRES_RET(prng_new(PRNG_SPLITMIX64, 9), return EXIT_FAILURE);
    LOCAL_SET(prng, smp2) = CRES_RET(prng_new(PRNG_SPLITMIX64, 9), return EXIT_FAILURE);
    LOCAL_SET(prng, smc) = CRES_RET(prng_split(smp), return EXIT_FAILURE);
    LOCAL_SET(prng, smc2) = CRES_RET(prng_split(smp2), return EXIT_FAILURE);
    for (int i = 0; i < 4; i++)
        if (prng_u64(smc) != prng_u64(smc2))    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int prng_dist_test(void)
{
    static const prng_alg algs[] = { PRNG_XOSHIRO256PP, PRNG_SPLITMIX64, PRNG_MT19937_64 };

    for (size_t a = 0; a < array_size(algs); a++) {
        LOCAL_SET(prng, p) = CRES_RET(prng_new(algs[a], 1u << a), return EXIT_FAILURE);
        bool lo_hit = false, hi_hit = false;

        for (int i = 0; i < 1000; i++) {
            float f = prng_unit(p);
            double d = prng_unitd(p);

            if (f < 0.0f || f >= 1.0f)          return EXIT_FAILURE;
            if (d < 0.0 || d >= 1.0)            return EXIT_FAILURE;

            int v = CRES_RET(prng_range(p, -5, 5), return EXIT_FAILURE);
            if (v < -5 || v >= 5)               return EXIT_FAILURE;
            lo_hit |= v == -5;
            hi_hit |= v == 4;
        }

        /* 1000 draws over a span of 10 miss an endpoint with p ~ 1e-46 */
        if (!lo_hit || !hi_hit)                 return EXIT_FAILURE;
    }

    LOCAL_SET(prng, p) = CRES_RET(prng_new(PRNG_DEFAULT, 0), return EXIT_FAILURE);

    /*
     * A span wider than INT_MAX: the wrapped sum would land on the right
     * value anyway, so this case is here for UBSan, which reports the
     * overflow it hides
     */
    for (int i = 0; i < 1000; i++) {
        int v = CRES_RET(prng_range(p, INT_MIN, INT_MAX), return EXIT_FAILURE);
        if (v == INT_MAX)                       return EXIT_FAILURE;
    }

    cres(int) empty = prng_range(p, 3, 3);
    if (!IS_CERR(empty))                        return EXIT_FAILURE;
    empty = prng_range(p, 5, -5);
    if (!IS_CERR(empty))                        return EXIT_FAILURE;
    cresp(prng) bad = prng_new((prng_alg)4711, 0);
    if (!IS_CERR(bad))                          return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "prng reference vectors", .test = prng_vectors_test },
    { .name = "prng split", .test = prng_split_test },
    { .name = "prng distributions", .test = prng_dist_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
