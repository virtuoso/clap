// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "common.h"
#include "memory.h"
#include "util.h"

typedef struct alloc_tag {
    size_t  size;
    // bool    freed;
    // uint8_t __reserved0[sizeof(size_t) - sizeof(bool)];
    void    *page;
    uint8_t buf[0];
} alloc_tag;

typedef struct mem_conf {
    size_t  page_size;
    size_t  frame_limit;
} mem_conf;

static mem_conf conf;

typedef struct arena_page {
    struct list entry;
    void        *addr;
    size_t      size;
    size_t      usage;
} arena_page;

typedef struct mem_state {
    struct list frame_list;
    arena_page  *frame_top;
    size_t      frame_arenas_nr;
    size_t      frame_arena_sz;
    // size_t      frame_arena_free_sz;
    size_t      frame_allocs;
    size_t      frame_frees;
    int         in_frame;
} mem_state;

static mem_state state;

void mem_init(void)
{
    conf.page_size = getpagesize();
    conf.frame_limit = conf.page_size;
    list_init(&state.frame_list);
}

static inline size_t arena_page_free_bytes(arena_page *page)
{
    return page->size - page->usage;
}

static DEFINE_CLEANUP(arena_page, mem_free(*p))

static cerr mem_grow_frame_arena(size_t size)
{
    LOCAL_SET(arena_page, page) = mem_alloc(sizeof(*page));
    if (!page)
        return CERR_NOMEM;

    size = round_up(size, conf.page_size);

#ifdef CONFIG_BROWSER
    int err = posix_memalign(&page->addr, conf.page_size, size);
    dbg("### posix_memalign(): %d / %p\n", err, page->addr);
    if (err)
        return CERR_NOMEM;
#else
    page->addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (page->addr == MAP_FAILED)
        return CERR_NOMEM;
#endif /* CONFIG_BROWSER */

    page->usage = 0;
    page->size = size;
    list_append(&state.frame_list, &page->entry);
    state.frame_top = page;
    state.frame_arena_sz += size; /* XXX: zero out on clear */
    state.frame_arenas_nr++;
    page = NULL;

    return CERR_OK;
}

static void mem_free_frame_arena(arena_page *page)
{
#ifdef CONFIG_BROWSER
    free(page->addr);
#else
    munmap(page->addr, page->size);
#endif /* CONFIG_BROWSER */

    state.frame_arenas_nr--;
    state.frame_arena_sz -= page->size;
    list_del(&page->entry);
    free(page);

    if (!state.frame_arenas_nr)
        state.frame_top = NULL;
    else if (state.frame_top == page)
        state.frame_top = list_last_entry(&state.frame_list, arena_page, entry);
}

// static cerr mem_grow_frame_arena(size_t size)
// {
//     arena_page *page = mem_alloc(sizeof(*page));
//     if (!page)
//         return CERR_NOMEM;

//     size = round_up(size, conf.page_size);
//     page->addr = arena_alloc(size);
//     if (page->addr == MAP_FAILED) {
//         mem_free(page);
//         return CERR_NOMEM;
//     }

//     page->usage = 0;
//     page->size = size;
//     list_append(&state.frame_list, &page->entry);
//     state.frame_top = page;
//     state.frame_arena_sz += size; /* XXX: zero out on clear */
//     // state.frame_arena_free_sz += size;
//     state.frame_arenas_nr++;

//     return CERR_OK;
// }

#define TAG_FLAG_MASK   UINTPTR_CONST(0x3)
#define TAG_FREED       UINTPTR_CONST(1)
static inline arena_page *alloc_tag_page(alloc_tag *tag)
{
    return (arena_page *)((uintptr_t)tag->page & ~TAG_FLAG_MASK);
}

static inline bool alloc_tag_is_free(alloc_tag *tag)
{
    return !!((uintptr_t)tag->page & TAG_FREED);
}

static inline void alloc_tag_free(alloc_tag *tag)
{
    tag->page = (void *)((uintptr_t)tag->page | TAG_FREED);
}

static inline void alloc_tag_in_use(alloc_tag *tag)
{
    tag->page = (void *)((uintptr_t)tag->page & ~TAG_FREED);
}

static inline alloc_tag *alloc_tag_of(void *ptr)
{
    return container_of(ptr, alloc_tag, buf);
}

static inline size_t alloc_tag_data_size(alloc_tag *tag)
{
    return tag->size - sizeof(*tag);
}

static inline alloc_tag *alloc_tag_next(alloc_tag *tag)
{
    arena_page *page = alloc_tag_page(tag);

    if ((uintptr_t)tag + tag->size >= (uintptr_t)page->addr + page->usage)
        return NULL;

    return (void *)tag + tag->size;
}

static void mem_frame_free(void *ptr)
{
    alloc_tag *tag = alloc_tag_of(ptr);
    arena_page *page = alloc_tag_page(tag);

    alloc_tag_free(tag);

    /* Merge tag with the surroundings */
    /* If it's the first tag in the page, don't look for the preceeding one */
    if ((void *)tag == page->addr)
        goto follows;

    /* Find the previous tag */
    alloc_tag *prev;
    for (prev = page->addr;
         alloc_tag_next(prev) != tag;
         prev = alloc_tag_next(prev))
        ;
    /* If it's unallocated, consume tag */
    if (alloc_tag_is_free(prev)) {
        prev->size += tag->size;
        tag = prev;
    }

follows:
    /* if the following tag exists and is unallocated, consume it too */
    alloc_tag *next = alloc_tag_next(tag);
    if (next && alloc_tag_is_free(next))
        tag->size += next->size;

    if (!next) {
        page->usage -= tag->size;
        if (!page->usage) {
            mem_free_frame_arena(page);
            // munmap(page->addr, page->size);
            // list_del(&page->entry);
            // mem_free(page);
            // state.frame_arenas_nr--;
            // if (!state.frame_arenas_nr)
            //     state.frame_top = NULL;
            // else if (state.frame_top == page)
            //     state.frame_top = list_last_entry(&state.frame_list, arena_page, entry);
        }
    }
}

static inline void *alloc_tag_buf(void *ptr, size_t size, arena_page *page)
{
    alloc_tag *tag = ptr;
    tag->size = size;
    tag->page = page;
    alloc_tag_in_use(tag);
    return &tag->buf;
}

static void *free_slot_find_get(arena_page *page, size_t size)
{
    alloc_tag *tag;

    for (tag = page->addr; tag->size; tag = alloc_tag_next(tag))
        /* size includes sizeof(*tag) */
        if (alloc_tag_is_free(tag) && tag->size >= size) {
            if (tag->size - size > sizeof(*tag)) {
                alloc_tag *next = alloc_tag_next(tag);
                next->page = tag->page; /* inherit TAG_FREED from tag */
                next->size = tag->size - size;
                dbg("### offset %zu size %zu -> %zu remaining %zu\n",
                    (void *)tag->buf - page->addr, tag->size, size, next->size);
                tag->size = size;
            }
            alloc_tag_in_use(tag);
            return &tag->buf;
        }

    return NULL;
}

static void *mem_arena_page_alloc(arena_page *page, size_t size)
{
    void *ret;

    if (likely(size < arena_page_free_bytes(page))) {
        ret = page->addr + page->usage;
        page->usage += size;
        return alloc_tag_buf(ret, size, page);
    }

    dbg("### %p..%p off %zu sz %zu free %zu\n", page->addr, page->addr + page->size,
        page->usage, size, arena_page_free_bytes(page));
    // state.frame_arena_free_sz -= size;
    return free_slot_find_get(page, size);
}

static void *mem_top_arena_frame_alloc(size_t size)
{
    return mem_arena_page_alloc(state.frame_top, size);
}

void *mem_frame_alloc(size_t size)
{
    void *ret;

    size += sizeof(alloc_tag);
    size = round_up(size, sizeof(int) * 2);
    if (!state.frame_top) {
        cerr err = mem_grow_frame_arena(size);
        if (err != CERR_OK)
            return NULL;
    }

    /* fits in the topmost arena_page */
    ret = mem_top_arena_frame_alloc(size);
    if (ret)
        return ret;//alloc_tag_buf(ret, size, state.frame_top);

    /* allocate a new arena_page */
    cerr err = mem_grow_frame_arena(size);
    if (err != CERR_OK)
        return NULL;

    ret = mem_top_arena_frame_alloc(size);
    if (ret)
        return ret;//alloc_tag_buf(ret, size, state.frame_top);

    return NULL;
}

/* XXX: these all need to be static */
void *mem_frame_realloc(void *ptr, size_t size, size_t old_size)
{
    alloc_tag *tag = alloc_tag_of(ptr);

    if (unlikely(old_size && old_size != alloc_tag_data_size(tag))) {
        enter_debugger();
        return NULL;
    } else {
        old_size = alloc_tag_data_size(tag);
    }

    if (!size) {
        mem_frame_free(ptr);
        return NULL;
    }

    if (size < old_size)
        return ptr;

    void *ret = mem_frame_alloc(size);
    if (!ret)
        return ret;

    memcpy(ret, ptr, old_size);
    mem_frame_free(ptr);

    return ret;
}

void mem_frame_clear(void)
{
    arena_page *page, *iter;

    if (state.frame_arenas_nr == 0 || state.frame_arenas_nr == 1)
        return;

    size_t old_size = state.frame_arena_sz;

    list_for_each_entry_iter(page, iter, &state.frame_list, entry) {
        alloc_tag *tag, *tag_free;
        bool keep = false;

        for (tag_free = page->addr; tag_free->size; tag_free = alloc_tag_next(tag_free))
            if (alloc_tag_is_free(tag_free))
                break;

        for (tag = page->addr; tag->size; tag = alloc_tag_next(tag)) {
            if (!alloc_tag_is_free(tag)) {
                keep = true;
                // warn("object %p size %zu not freed\n", &tag->buf, tag->size);
                if (tag_free->size && tag_free < tag)
                    tag_free->size = (uintptr_t)tag - (uintptr_t)tag_free;
            } else {
                if (tag_free < tag)
                    tag_free->size += tag->size;
            }
            // err_on(!tag->freed, "object %p size %zu not freed\n", &tag->buf, tag->size);
        }

        if (keep)
            continue;

        // state.frame_arenas_nr--;
        // state.frame_arena_sz -= page->size;
        // munmap(page->addr, page->size);
        // list_del(&page->entry);
        // free(page);
        mem_free_frame_arena(page);
    }

    // state.frame_arenas_nr = 0;
    // state.frame_arena_free_sz = 0;
    // state.frame_arena_sz = 0;
    unused cerr err = mem_grow_frame_arena(old_size);
    assert(err == CERR_OK);
}

void mem_frame_begin(void)
{
    state.in_frame++;
}

void mem_frame_end(void)
{
    state.in_frame = 0;
    mem_frame_clear();
}

void mem_frame_limit(size_t limit)
{
    conf.frame_limit = limit;
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

    if (state.in_frame && params->per_frame && size <= conf.frame_limit) {
        size_t nr = params->nr;
        if (!nr)
            nr = 1;

        ret = mem_frame_alloc(size);
        if (!ret)
            goto out;
    } else {
        if (params->nr)
            ret = calloc(params->nr, size);
        else
            ret = malloc(size);
    }

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

    if (state.in_frame && params->per_frame && size <= conf.frame_limit) {
        if (params->old_size && params->old_size > total)
            return buf;

        ret = mem_frame_realloc(buf, total, params->old_size);
        if (!ret)
            goto out;
    } else {
        ret = realloc(buf, total);
    }
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

    if (state.in_frame && params->per_frame) {
        mem_frame_free(buf);
        return;
    }

    free(buf);
}

cerr _mem_vasprintf(const char *mod, char **ret, const char *fmt, va_list va)
{
    va_list va2;
    va_copy(va2, va);

    int len = vsnprintf(NULL, 0, fmt, va2);
    va_end(va2);

    if (len < 0)
        return CERR_INVALID_ARGUMENTS;

    *ret = mem_alloc(len + 1u);
    if (!*ret)
        return CERR_NOMEM;

    len = vsnprintf(*ret, len + 1u, fmt, va);
    if (len < 0)
        return CERR_INVALID_ARGUMENTS;

    return len;
}

cerr _mem_asprintf(const char *mod, char **ret, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    cerr err = _mem_vasprintf(mod, ret, fmt, va);
    va_end(va);

    return err;
}
