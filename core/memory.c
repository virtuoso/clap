// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "memory.h"
#include "util.h"

void *_mem_alloc(const char *mod, size_t size, const alloc_params *params)
{
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

void _mem_free(const char *mod, void *buf)
{
    free(buf);
}
