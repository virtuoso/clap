/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UTIL_H__
#define __CLAP_UTIL_H__

#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "logger.h"

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
#define xmin(a, b) (min((a), (b)) == (a) ? 0 : 1)
#define min3(a, b, c) (min(a, min(b, c)))
#define xmin3(a, b, c) ({ \
    typeof(a) __x = min3((a), (b), (c)); \
    int __w = 0; \
    if (__x == (b)) __w = 1; else if (__x == (c)) __w = 2; \
    __w; \
})
#define max(a, b) ((a) > (b) ? (a) : (b))
#define xmax(a, b) (max((a), (b)) == (a) ? 0 : 1)
#define max3(a, b, c) (max(a, max(b, c)))
#define xmax3(a, b, c) ({ \
    typeof(a) __x = max3((a), (b), (c)); \
    int __w = 0; \
    if (__x == (b)) __w = 1; else if (__x == (c)) __w = 2; \
    __w; \
})

#define CHECK_NVAL(_st, _q, _val) ({ \
    typeof(_val) __x = _q (_st); \
    err_on_cond(__x != (_val), _st != _val, "failed: %ld\n", (long)__x); \
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

static inline const char *str_basename(const char *str)
{
    const char *p = strrchr(str, '/');

    if (p)
        return p + 1;

    return str;
}

struct darray {
    void            *array;
    size_t          elsz;
    unsigned int    nr_el;
};

#define darray(_type, _name) \
union { \
    _type           *x; \
    struct darray   da; \
} _name;

#define darray_init(_da) { (_da)->da.elsz = sizeof(*(_da)->x); (_da)->da.nr_el = 0; (_da)->x = NULL; }
static inline void *darray_get(struct darray *da, unsigned int el)
{
    if (el >= da->nr_el)
        return NULL;
    return da->array + da->elsz * el;
}

void *darray_resize(struct darray *da, unsigned int nr_el);
void *darray_add(struct darray *da);
void *darray_insert(struct darray *da, int idx);
void darray_delete(struct darray *da, int idx);
void darray_clearout(struct darray *da);
#define DA(_da, _type, _idx) ((_type *)darray_get((_da), _idx))
#define darray_for_each(_el, _da) \
    for (_el = &(_da)->x[0]; _el != &(_da)->x[(_da)->da.nr_el]; _el++)

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

struct hashmap {
    struct list *buckets;
    struct list list;
    size_t      nr_buckets;
    unsigned long (*hash)(struct hashmap *hm, unsigned int key);
};

struct hashmap_entry {
    struct list     entry;
    struct list     list_entry;
    void            *value;
    unsigned int    key;
};

int hashmap_init(struct hashmap *hm, size_t nr_buckets);
void *hashmap_find(struct hashmap *hm, unsigned int key);
void hashmap_delete(struct hashmap *hm, unsigned int key);
int hashmap_insert(struct hashmap *hm, unsigned int key, void *value);
void hashmap_done(struct hashmap *hm);
void hashmap_for_each(struct hashmap *hm, void (*cb)(void *value, void *data), void *data);

struct bitmap {
    unsigned long   *mask;
    size_t          size;
};

void bitmap_init(struct bitmap *b, size_t bits);
void bitmap_done(struct bitmap *b);
void bitmap_set(struct bitmap *b, unsigned int bit);
bool bitmap_is_set(struct bitmap *b, unsigned int bit);
bool bitmap_includes(struct bitmap *b, struct bitmap *subset);

void *memdup(const void *x, size_t size);

static inline int clamp(int x, int floor, int ceil)
{
    if (x > ceil)
        return ceil;
    else if (x < floor)
        return floor;
    return x;
}

static inline float clampf(float x, float floor, float ceil)
{
    if (x > ceil)
        return ceil;
    else if (x < floor)
        return floor;
    return x;
}

static inline double clampd(double x, double floor, double ceil)
{
    if (x > ceil)
        return ceil;
    else if (x < floor)
        return floor;
    return x;
}

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
}

static inline bool timespec_nonzero(struct timespec *ts)
{
    return !!ts->tv_sec || !!ts->tv_nsec;
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

typedef void (*exit_handler_fn)(int);
int exit_cleanup(exit_handler_fn);
void exit_cleanup_run(int status);

#endif /* __CLAP_UTIL_H__ */

