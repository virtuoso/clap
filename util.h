#ifndef __CLAP_UTIL_H__
#define __CLAP_UTIL_H__

#include <stdio.h>
#include <ctype.h>

void cleanup__fd(int *fd);
void cleanup__FILEp(FILE **f);
void cleanup__malloc(void **x);
void cleanup__charp(char **s);

#define CU(x) __attribute__((cleanup(cleanup__ ## x)))
#define CUX(x) CU(x) = NULL
#define LOCAL(t, x) t *x CUX(t ## p)

#define nonstring __attribute__((nonstring))
#define unused __attribute__((unused))
#define notrace __attribute__((no_instrument_function))

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#define array_size(x) (sizeof(x) / sizeof(*x))
#ifndef offsetof
#define offsetof(a, b) __builtin_offsetof(a, b)
#endif /* offsetof */
#define container_of(node, type, field) ((type *)((void *)(node)-offsetof(type, field)))

#define __stringify(x) (# x)

void *memdup(const void *x, size_t size);

static inline const char *skip_nonspace(const char *pos)
{
    for (; *pos && !isspace(*pos); pos++)
        ;
    return pos;
}

static inline const char *skip_space(const char *pos)
{
    for (; *pos && isspace(*pos); pos++)
        ;
    return pos;
}

static inline const char *skip_to_eol(const char *pos)
{
    for (; *pos && *pos != '\n'; pos++)
        ;
    return pos;
}

static inline const char *skip_to_new_line(const char *pos)
{
    pos = skip_to_eol(pos);
    return *pos ? skip_space(pos) : pos;
}

/*
 * A chain is something that has ->next pointer to the same
 * type as self.
 */
#define __is_chain(__c) \
    do { typeof(__c) _x; _x->next = (__c); } while (0)

#define chain_append(__c, __o)                              \
    ({                                                      \
        typeof(*(__c)) _x, **_lastp;                        \
        __is_chain((__c));                                  \
        for (lastp = &(__c); *_lastp; _lastp = &((*_lastp)->next));   \
        _x = malloc(sizeof(_x));                            \
        if (_x) {                                           \
            *_lastp = &_x;                                  \
        }                                                   \
        _x                                                  \
    })

#define chain_prepend(__c, __o)                             \
    ({                                                      \
        typeof(*(__c)) _x;                                  \
        __is_chain((__c));                                  \
        _x = malloc(sizeof(_x));                            \
        if (_x) {                                           \
            _x->link = (__c);                               \
            *(&(__c)) = &(_x->link);                        \
        }                                                   \
        _x                                                  \
    })

#define chain_for_each(__c, __o) \
    for (__o = (__c); __o; __o = (__o)->next)

typedef void (*exit_handler_fn)(int);
int exit_cleanup(exit_handler_fn);
void exit_cleanup_run(int status);

#endif /* __CLAP_UTIL_H__ */

