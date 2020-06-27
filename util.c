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

void *memdup(const void *x, size_t size)
{
    void *r = malloc(size);
    if (r) {
        memcpy(r, x, size);
    }
    return r;
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
