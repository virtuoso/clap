#ifndef __CLAP_UTIL_H__
#define __CLAP_UTIL_H__

#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

typedef unsigned char uchar;

void cleanup__fd(int *fd);
void cleanup__FILEp(FILE **f);
void cleanup__malloc(void **x);
void cleanup__charp(char **s);
void cleanup__ucharp(uchar **s);

#define CU(x) __attribute__((cleanup(cleanup__ ## x)))
#define CUX(x) CU(x) = NULL
#define LOCAL_(t, ts, x) t *x CUX(ts ## p)
#define LOCAL(t, x) LOCAL_(t, t, x)

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

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define CHECK_NVAL(_st, _q, _val) ({ \
    typeof(_val) __x = _q (_st); \
    err_on_cond(__x != (_val), _st != _val, "failed: %d\n", __x); \
    __x; \
})
#define CHECK_VAL(_st, _val) CHECK_NVAL(_st,, _val)
#define CHECK(_st) CHECK_NVAL(_st, !!, true)
#define CHECK0(_st) CHECK_VAL(_st, 0)

static inline bool str_endswith(const char *str, const char *sfx)
{
    size_t sfxlen = strlen(sfx);
    size_t len = strlen(str);

    if (len < sfxlen)
        return false;

    if (!strncmp(str + len - sfxlen, sfx, sfxlen))
        return true;

    return false;
}

struct list {
    struct list *prev, *next;
};

#define EMPTY_LIST(__list) { .next = &(__list), .prev = &(__list) }
#define DECLARE_LIST(__list) struct list __list = EMPTY_LIST(__list)

static inline bool list_empty(struct list *head)
{
    return head->next == head && head->prev == head;
}

static inline void list_init(struct list *head)
{
    head->next = head->prev = head;
}

static inline void list_prepend(struct list *head, struct list *el)
{
    el->next         = head->next;
    el->prev         = head;
    head->next->prev = el;
    head->next       = el;
}

static inline void list_append(struct list *head, struct list *el)
{
    el->next         = head;
    el->prev         = head->prev;
    head->prev->next = el;
    head->prev       = el;
}

static inline void list_del(struct list *el)
{
    el->prev->next = el->next;
    el->next->prev = el->prev;
    list_init(el);
}

#define list_entry(__el, __type, __link) ( (__type *)(((char *)__el) - offsetof(__type, __link)) )
#define list_first_entry(__list, __type, __link) list_entry((__list)->next, __type, __link)
#define list_last_entry(__list, __type, __link) list_entry((__list)->prev, __type, __link)
#define list_next_entry(__ent, __link) list_entry(__ent->__link.next, typeof(*__ent), __link)
#define list_prev_entry(__ent, __link) list_entry(__ent->__link.prev, typeof(*__ent), __link)
#define list_for_each(__el, __list)      for (__el = (__list)->next; __el != (__list); __el = __el->next)
#define list_for_each_entry(__ent, __list, __link)                                                                     \
    for (__ent = list_first_entry((__list), typeof(*__ent), __link); &(__ent->__link) != (__list);                     \
         __ent = list_next_entry(__ent, __link))
#define list_for_each_entry_iter(__ent, __it, __list, __link)                                                                     \
    for (__ent = list_first_entry((__list), typeof(*__ent), __link), __it = list_next_entry(__ent, __link); \
         &(__ent->__link) != (__list);             \
         __ent = __it, __it = list_next_entry(__it, __link))

void *memdup(const void *x, size_t size);

struct timespec64 {
#if __SIZEOF_LONG__ == 4
    unsigned long long tv_sec;
    unsigned long long tv_nsec;
#else
    unsigned long tv_sec;
    unsigned long tv_nsec;
#endif
};

static inline void timespec_to_64(struct timespec *ts, struct timespec64 *ts64)
{
    ts64->tv_sec = ts->tv_sec;
    ts64->tv_nsec = ts->tv_nsec;
}

static inline void timespec_from_64(struct timespec *ts, struct timespec64 *ts64)
{
    /* XXX: truncation and rounding up */
    ts->tv_sec = ts64->tv_sec;
    ts->tv_nsec = ts64->tv_nsec;
}

static inline void timespec_diff(struct timespec *a, struct timespec *b, struct timespec *diff)
{
    if ((b->tv_nsec - a->tv_nsec) < 0) {
        diff->tv_sec  = b->tv_sec - a->tv_sec - 1;
        diff->tv_nsec = b->tv_nsec - a->tv_nsec + 1000000000;
    } else {
        diff->tv_sec  = b->tv_sec - a->tv_sec;
        diff->tv_nsec = b->tv_nsec - a->tv_nsec;
    }

    return;
}

static inline const char  *skip_nonspace(const char *pos)
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
        __is_chain((__c));                \
        for (lastp = &(__c); *_lastp; _lastp = &((*_lastp)->next));   \
        _x = malloc(sizeof(_x));                   \
        if (_x) {                                  \
            *_lastp = &_x;                         \
        }                                          \
        _x                                         \
    })

#define chain_prepend(__c, __o)                             \
    ({                                                      \
        typeof(*(__c)) _x;                                  \
        __is_chain((__c));                                  \
        _x = malloc(sizeof(_x));                            \
        if (_x) {                                           \
            _x->link = (__c);                               \
            *(&(__c)) = &(_x->link);                        \
        }                                          \
        _x                                         \
    })

#define chain_for_each(__c, __o) \
    for (__o = (__c); __o; __o = (__o)->next)

        typedef void (*exit_handler_fn)(int);
int exit_cleanup(exit_handler_fn);
void exit_cleanup_run(int status);

#endif /* __CLAP_UTIL_H__ */

