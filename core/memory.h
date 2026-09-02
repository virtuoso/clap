/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_MEMORY_H__
#define __CLAP_MEMORY_H__

#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "error.h"

void mem_frame_begin(void);
void mem_frame_end(void);

typedef struct alloc_params {
    size_t          nr;
    uint64_t        fatal_fail  : 1,
                    zero        : 1,
                    __reserved0 : 62;
} alloc_params;

typedef struct realloc_params {
    uint64_t        fatal_fail  : 1,
                    __reserved0 : 63;
    size_t          old_size;
    const char      *mod;
} realloc_params;

typedef struct free_params {
    size_t          size;
    const char      *mod;
} free_params;

#define mem_alloc(_size, args...) \
    _mem_alloc(MODNAME, (_size), &(alloc_params){ args })
must_check void *_mem_alloc(const char *mod, size_t size, const alloc_params *params);

#define mem_realloc(_buf, _size, args...) ({ \
    typeof(_buf) __x = _mem_realloc_array(MODNAME, (_buf), 1, (_size), \
                                          &(realloc_params){ args }); \
    __x; \
})

#define mem_realloc_array(_buf, _nmemb, _size, args...) ({ \
    typeof(_buf) __x = _mem_realloc_array(MODNAME, (_buf), (_nmemb), (_size), \
                                          &(realloc_params){ args }); \
    __x; \
})
must_check void *_mem_realloc_array(const char *mod, void *buf, size_t nmemb, size_t size,
                                    const realloc_params *params);

#define mem_free(_buf, args...) \
    _mem_free(MODNAME, (_buf), &(free_params){ args })
void _mem_free(const char *mod, void *buf, const free_params *params);

#ifdef CONFIG_BROWSER
/**
 * MEM_HEAP_RESERVE - heap that mem_heap_has_room() keeps for everything else
 *
 * An allocation that passes the check lives on while the rest of the
 * engine, on this thread and others, keeps allocating; all of that comes
 * out of the reserve, and so does dlmalloc's overhead on the allocation
 * itself: its chunk header, the padding memalign() adds, the top chunk's
 * foot and the break's 4 KiB steps, under 4.2 KiB. A loaded browser
 * session doesn't grow the heap as it plays (not at all over 70 s at
 * 1924x1080, walking about), so the reserve is room for the transient
 * allocations of other threads.
 */
#define MEM_HEAP_RESERVE    (32u << 20)
#endif /* CONFIG_BROWSER */

/**
 * mem_heap_has_room() - check that an allocation leaves the heap its reserve
 * @size:   bytes the caller is about to allocate
 *
 * Browser builds run on a fixed heap, and an allocation that doesn't fit
 * aborts the page instead of returning NULL. An allocation the engine
 * can do without asks here first and skips its work on a no. The heap
 * past the break and the free top chunk count; blocks freed into
 * dlmalloc's bins don't, so the answer can be no for an allocation that
 * would fit. Counting the top chunk relies on dlmalloc growing it in
 * place, which it stops doing for good once a request near 4 GiB fails;
 * after that, a yes can be wrong. Other threads keep allocating after
 * the answer, out of MEM_HEAP_RESERVE. Native heaps grow on demand, and
 * their malloc() returns NULL when they can't.
 * Return: true if @size bytes leave MEM_HEAP_RESERVE free; always true in
 * native builds
 */
#ifdef CONFIG_BROWSER
bool mem_heap_has_room(size_t size);
#else
static inline bool mem_heap_has_room(size_t size) { return true; }
#endif /* !CONFIG_BROWSER */

#define mem_vasprintf(_ret, _fmt, _ap) \
    _mem_vasprintf(MODNAME, (_ret), (_fmt), (_ap))
cres_check(int) _mem_vasprintf(const char *mod, char **ret, const char *fmt, va_list ap);

#define mem_asprintf(_ret, _fmt, args...) \
    _mem_asprintf(MODNAME, (_ret), (_fmt), ## args)
cres_check(int) _mem_asprintf(const char *mod, char **ret, const char *fmt, ...) __printf(3, 4);

#endif /* __CLAP_MEMORY_H__ */
