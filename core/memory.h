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

#define mem_realloc(_buf, _size, args...) \
    _mem_realloc_array(MODNAME, (_buf), 1, (_size), &(realloc_params){ args })
#define mem_realloc_array(_buf, _nmemb, _size, args...) \
    _mem_realloc_array(MODNAME, (_buf), (_nmemb), (_size), &(realloc_params){ args })
must_check void *_mem_realloc_array(const char *mod, void *buf, size_t nmemb, size_t size,
                                    const realloc_params *params);

#define mem_free(_buf, args...) \
    _mem_free(MODNAME, (_buf), &(free_params){ args })
void _mem_free(const char *mod, void *buf, const free_params *params);

#define mem_vasprintf(_ret, _fmt, _ap) \
    _mem_vasprintf(MODNAME, (_ret), (_fmt), (_ap))
cerr _mem_vasprintf(const char *mod, char **ret, const char *fmt, va_list ap);

#define mem_asprintf(_ret, _fmt, args...) \
    _mem_asprintf(MODNAME, (_ret), (_fmt), ## args)
cerr _mem_asprintf(const char *mod, char **ret, const char *fmt, ...);

#endif /* __CLAP_MEMORY_H__ */
