// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* XXX: replace with mem_*() equivalents in core/memory.c */
char *strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *out = malloc(len + 1);

    if (!out)
        return NULL;

    memcpy(out, s, len);
    out[len] = '\0';

    return out;
}
