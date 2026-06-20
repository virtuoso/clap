// SPDX-License-Identifier: Apache-2.0
//
// The std::mt19937_64 backend for prng.c, kept in its own translation unit
// so the rest of prng stays plain C. The engine state is constructed into
// caller-provided storage; it is trivially destructible, so freeing that
// storage is all the teardown there is.
#include <new>
#include <random>
#include <type_traits>

#include "prng-mt.h"

static_assert(std::is_trivially_destructible<std::mt19937_64>::value,
              "prng.c frees mt19937_64 state without running a destructor");

size_t prng_mt19937_size(void)
{
    return sizeof(std::mt19937_64);
}

size_t prng_mt19937_align(void)
{
    return alignof(std::mt19937_64);
}

void prng_mt19937_init(void *state, uint64_t seed)
{
    new (state) std::mt19937_64(seed);
}

uint64_t prng_mt19937_next(void *state)
{
    return (*static_cast<std::mt19937_64 *>(state))();
}
