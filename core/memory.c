// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "memory.h"
#include "util.h"

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
