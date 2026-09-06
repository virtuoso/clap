// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdlib.h>
#include "memory.h"
#include "test.h"
#include "util.h"

#ifdef CONFIG_BROWSER
#include <malloc.h>
#include <unistd.h>
#include <emscripten/heap.h>

/*
 * The tests own the heap: nothing else allocates while they run, so the
 * heap past the break and the top chunk only move when they allocate.
 * A ballast leaves known room by construction; every allocation after it
 * is one mem_heap_has_room() has said yes to, or one inside the room the
 * ballast left or the reserve the helper promised: a wrong answer aborts
 * the run.
 */

/** BIG_SIZE - an allocation the filled heap turns down */
#define BIG_SIZE    (16u << 20)
/** SMALL_SIZE - an allocation the filled heap still takes */
#define SMALL_SIZE  (1u << 20)
/** SPARE - what the ballast leaves past MEM_HEAP_RESERVE: room for SMALL_SIZE, not BIG_SIZE */
#define SPARE       (4u << 20)
/** FRAG_SIZE - a freed block that takes the free heap past BIG_SIZE without holding BIG_SIZE */
#define FRAG_SIZE   (14u << 20)
/** PIN_SIZE - a block that keeps a freed fragment out of the top chunk, too big for the small bins */
#define PIN_SIZE    (1u << 20)

/*
 * The tests only check their blocks for NULL, which lets the compiler drop
 * an allocation altogether; storing each block here keeps it
 */
static void *volatile last_block;

static void *heap_alloc(size_t size)
{
    void *block = mem_alloc(size);

    last_block = block;
    return block;
}

/* The heap past the break */
static size_t heap_past_break(void)
{
    return emscripten_get_heap_max() - (uintptr_t)sbrk(0);
}

/*
 * A block that leaves MEM_HEAP_RESERVE plus @spare free, counting the top
 * chunk, which holds whatever the tests before this one freed
 */
static void *heap_fill(size_t spare)
{
    size_t avail = mallinfo().keepcost + heap_past_break();

    if (avail < MEM_HEAP_RESERVE + spare)   return NULL;

    return heap_alloc(avail - MEM_HEAP_RESERVE - spare);
}

/* An empty heap takes a big allocation and a small one */
static int heap_room_empty(void)
{
    if (!mem_heap_has_room(SMALL_SIZE)) return EXIT_FAILURE;
    if (!mem_heap_has_room(BIG_SIZE))   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* A filled heap turns the big allocation down and takes the small one */
static int heap_room_filled(void)
{
    LOCAL_SET(void, ballast) = heap_fill(SPARE);
    if (!ballast)                       return EXIT_FAILURE;

    if (mem_heap_has_room(BIG_SIZE))    return EXIT_FAILURE;
    if (!mem_heap_has_room(SMALL_SIZE)) return EXIT_FAILURE;

    /* the yes was real: SMALL_SIZE comes out, and the reserve is still there */
    LOCAL_SET(void, small) = heap_alloc(SMALL_SIZE);
    if (!small)                         return EXIT_FAILURE;
    if (!mem_heap_has_room(0))          return EXIT_FAILURE;

    LOCAL_SET(void, reserve) = heap_alloc(MEM_HEAP_RESERVE / 2);
    if (!reserve)                       return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * A block freed next to the top chunk merges into it while the break stays
 * put: the space past the break alone says no, the top chunk makes it a yes
 */
static int heap_room_top_chunk(void)
{
    LOCAL_SET(void, ballast) = heap_fill(BIG_SIZE + SPARE);
    if (!ballast)                                           return EXIT_FAILURE;
    if (!mem_heap_has_room(BIG_SIZE))                       return EXIT_FAILURE;

    {
        LOCAL_SET(void, big) = heap_alloc(BIG_SIZE);
        if (!big)                                           return EXIT_FAILURE;
        if (mem_heap_has_room(BIG_SIZE))                    return EXIT_FAILURE;
    }

    if (heap_past_break() >= MEM_HEAP_RESERVE + BIG_SIZE)   return EXIT_FAILURE;
    if (!mem_heap_has_room(BIG_SIZE))                       return EXIT_FAILURE;

    LOCAL_SET(void, again) = heap_alloc(BIG_SIZE);
    if (!again)                                             return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * Free blocks that can't hold the allocation don't count: a freed fragment,
 * pinned away from the top chunk, takes the heap's free space past what
 * BIG_SIZE needs, but it doesn't hold BIG_SIZE, and neither does the top
 * chunk with the space past the break
 */
static int heap_room_fragments(void)
{
    LOCAL_SET(void, ballast) = heap_fill(SPARE + FRAG_SIZE + PIN_SIZE);
    if (!ballast)                       return EXIT_FAILURE;

    LOCAL(void, pin);
    {
        LOCAL_SET(void, frag) = heap_alloc(FRAG_SIZE);
        pin = heap_alloc(PIN_SIZE);
        if (!frag || !pin)              return EXIT_FAILURE;
    }

    if (mem_heap_has_room(BIG_SIZE))    return EXIT_FAILURE;
    if (!mem_heap_has_room(SMALL_SIZE)) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* Sizes past the whole heap are a no, without wrapping around the reserve */
static int heap_room_huge(void)
{
    if (mem_heap_has_room(SIZE_MAX))                    return EXIT_FAILURE;
    if (mem_heap_has_room(emscripten_get_heap_max()))   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
#else
/* Native heaps grow on demand: the answer is always yes */
static int heap_room_native(void)
{
    if (!mem_heap_has_room(SIZE_MAX))   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
#endif /* !CONFIG_BROWSER */

static const struct test tests[] = {
#ifdef CONFIG_BROWSER
    { .name = "heap room: empty heap",  .test = heap_room_empty },
    { .name = "heap room: filled heap", .test = heap_room_filled },
    { .name = "heap room: top chunk",   .test = heap_room_top_chunk },
    { .name = "heap room: fragments",   .test = heap_room_fragments },
    { .name = "heap room: huge sizes",  .test = heap_room_huge },
#else
    { .name = "heap room: native",      .test = heap_room_native },
#endif /* !CONFIG_BROWSER */
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
