// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "util.h"

void cleanup__fd(int *fd)
{
    close(*fd);
}

void cleanup__FILEp(FILE **f)
{
    if (*f)
        fclose(*f);
}

void cleanup__malloc(void **x)
{
    free(*x);
}

void cleanup__charp(char **s)
{
    free(*s);
}

void cleanup__ucharp(uchar **s)
{
    free(*s);
}

void *memdup(const void *x, size_t size)
{
    void *r = malloc(size);
    if (r) {
        memcpy(r, x, size);
    }
    return r;
}

void *darray_resize(struct darray *da, unsigned int nr_el)
{
    void *new = realloc(da->array, nr_el * da->elsz);

    if (!new)
        return NULL;

    da->array = new;
    if (nr_el > da->nr_el)
        memset(new + da->nr_el * da->elsz, 0, (nr_el - da->nr_el) * da->elsz);
    da->nr_el = nr_el;

    return da->array;
}

void *darray_add(struct darray *da)
{
    void *new = darray_resize(da, da->nr_el + 1);

    if (!new)
        return NULL;

    new = darray_get(da, da->nr_el - 1);
    memset(new, 0, da->elsz);

    return new;
}

void darray_clearout(struct darray *da)
{
    free(da->array);
    da->array = NULL;
    da->nr_el = 0;
}

struct chain_link {
    struct chain_link   *next;
};

/*static struct chain_link **chain_last(struct chain_link **linkp)
{
    struct chain_link **lastp;

    for (lastp = linkp; *lastp; lastp = &((*lastp)->next))
        ;
    
    return lastp;
}*/


/*int chain_for_each(struct chain_link *link, int (*fn)(struct chain_link *))
{
    int ret;

    for (; link; link = link->next)
        ret |= fn(link)
}*/

struct exit_handler {
    exit_handler_fn     fn;
    struct exit_handler *next;
};

static struct exit_handler *ehs;

notrace int exit_cleanup(exit_handler_fn fn)
{
    struct exit_handler *eh, **lastp;

    eh = malloc(sizeof(*eh));
    if (!eh)
        return -ENOMEM;

    memset(eh, 0, sizeof(*eh));
    eh->fn = fn;

    for (lastp = &ehs; *lastp; lastp = &((*lastp)->next))
        ;

    *lastp = eh;

    return 0;
}

void exit_cleanup_run(int status)
{
    struct exit_handler *eh;

    /* XXX: free all the ehs too */
    for (eh = ehs; eh; eh = eh->next) {
        eh->fn(status);
    }
    fflush(stdout);
}

static void __attribute__((destructor)) do_exit(void)
{
    exit_cleanup_run(0);
}
