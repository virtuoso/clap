// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include "common.h"
#ifdef CONFIG_BROWSER
#include <malloc.h>
#include <unistd.h>
#include <emscripten/heap.h>
#endif /* CONFIG_BROWSER */
#include "memory.h"
#include "util.h"

typedef struct mem_state {
    int         in_frame;
} mem_state;

static thread_local mem_state state;

void mem_frame_begin(void)
{
    state.in_frame++;
}

void mem_frame_end(void)
{
    state.in_frame = 0;
}

void *_mem_alloc(const char *mod, size_t size, const alloc_params *params)
{
    if (params->__reserved0)
        return NULL;

    mod = str_basename(mod);

    void *ret = NULL;
    size_t total;

    if (mul_overflow(params->nr, size, &total))
        goto out;

    if (params->nr)
        ret = calloc(params->nr, size);
    else
        ret = malloc(size);

out:
    if (!ret) {
        if (params->fatal_fail)
            enter_debugger();

        return ret;
    }

    if (!params->nr && params->zero)
        memset(ret, 0, size);

    return ret;
}

void *_mem_realloc_array(const char *mod, void *buf, size_t nmemb, size_t size,
                         const realloc_params *params)
{
    if (params->__reserved0)
        return NULL;

    if (params->mod)
        mod = params->mod;

    mod = str_basename(mod);

    void *ret = NULL;
    size_t total;

    if (mul_overflow(nmemb, size, &total))
        goto out;

    ret = realloc(buf, total);
// #pragma GCC diagnostic push
// #pragma GCC diagnostic ignored "-Wuse-after-free"
//     dbg("### [%s] ptr: %p -> %p total: %zu\n", mod, buf, ret, total);
// #pragma GCC diagnostic pop

out:
    if (params->fatal_fail && total && !ret)
        enter_debugger();
    
    return ret;
}

void _mem_free(const char *mod, void *buf, const free_params *params)
{
    if (params->mod)
        mod = params->mod;

    mod = str_basename(mod);

    free(buf);
}

#ifdef CONFIG_BROWSER
/**
 * heap_unclaimed() - the heap past the break
 *
 * The break only ever moves up.
 * Return: the bytes between the break and the heap's maximum size
 */
static size_t heap_unclaimed(void)
{
    return emscripten_get_heap_max() - (uintptr_t)sbrk(0);
}

/**
 * heap_room_fits() - whether @room holds @size with MEM_HEAP_RESERVE to spare
 * @room:   the free bytes
 * @size:   the bytes to allocate
 *
 * Return: true if @size fits in @room past MEM_HEAP_RESERVE
 */
static bool heap_room_fits(size_t room, size_t size)
{
    return room >= MEM_HEAP_RESERVE && room - MEM_HEAP_RESERVE >= size;
}

bool mem_heap_has_room(size_t size)
{
    if (heap_room_fits(heap_unclaimed(), size)) return true;

    /*
     * dlmalloc grows the top chunk in place, so its free space counts too.
     * mallinfo() walks every chunk under the malloc lock to report it, so
     * it only runs when the space past the break falls short. The top goes
     * first: the break never comes down, so an allocation between the two
     * reads shrinks the sum, never grows it
     */
    size_t top = mallinfo().keepcost;

    return heap_room_fits(top + heap_unclaimed(), size);
}
#endif /* CONFIG_BROWSER */

cres(int) _mem_vasprintf(const char *mod, char **ret, const char *fmt, va_list va)
{
    va_list va2;
    va_copy(va2, va);

    int len = vsnprintf(NULL, 0, fmt, va2);
    va_end(va2);

    if (len < 0)
        return cres_error(int, CERR_INVALID_ARGUMENTS);

    *ret = mem_alloc(len + 1u);
    if (!*ret)
        return cres_error(int, CERR_NOMEM);

    len = vsnprintf(*ret, len + 1u, fmt, va);
    if (len < 0)
        return cres_error(int, CERR_INVALID_ARGUMENTS);

    return cres_val(int, len);
}

cres(int) _mem_asprintf(const char *mod, char **ret, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    cres(int) res = _mem_vasprintf(mod, ret, fmt, va);
    va_end(va);

    return res;
}
