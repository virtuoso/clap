/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UTIL_H__
#define __CLAP_UTIL_H__

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "compiler.h"
#include "error.h"
#include "linmath.h"
#include "logger.h"

typedef unsigned char uchar;

#define DECLARE_CLEANUP(t) void cleanup__## t ## p(t **p)
#define DEFINE_CLEANUP(t, fn) \
DECLARE_CLEANUP(t) { fn; }

void cleanup__fd(int *fd);
DECLARE_CLEANUP(FILE);
DECLARE_CLEANUP(void);
DECLARE_CLEANUP(char);
DECLARE_CLEANUP(uchar);

#define NOCU(x) ({ typeof(x) _x = x; x = NULL; _x; })
#define CUX(x) CU(x) = NULL
#define LOCAL_(t, ts, x) t *x CUX(ts ## p)
#define LOCAL(t, x) LOCAL_(t, t, x)
#define LOCAL_SET_(t, ts, x) t *x CU(ts ## p)
#define LOCAL_SET(t, x) LOCAL_SET_(t, t, x)

#define array_size(x) (sizeof(x) / sizeof(*x))
#define container_of(node, type, field) ((type *)((void *)(node)-offsetof(type, field)))

#define __stringify(x) (# x)

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923132169163975144
#endif
#ifndef M_PI_4
#define M_PI_4 0.785398163397448309615660845819875721
#endif

static inline float to_radians(float degrees)
{
    return degrees * M_PI / 180.0;
}

static inline float to_degrees(float radians)
{
    return radians / M_PI * 180.0;
}

static inline float clamp_radians(float angle)
{
    return fabsf(angle) <= M_PI ? angle : (angle - copysignf(M_PI * 2.0, angle));
}

static inline float clamp_degrees(float angle)
{
    return fabsf(angle) <= 180.0 ? angle : (angle - copysignf(360.0, angle));
}

/**
 * aabb_center() - calculate AABB's center
 * @aabb:   AABB box
 * @center: center
 *
 * Write @aabb's center to @center.
 */
static inline void aabb_center(vec3 const aabb[2], vec3 center)
{
    vec3_sub(center, aabb[1], aabb[0]);
    vec3_scale(center, center, 0.5);
    vec3_add(center, center, aabb[0]);
}

/**
 * vertex_array_xlate_aabb_calc() - calculate AABB from a vertex array
 * @aabb:   AABB box output
 * @vx:     vertex array
 * @vxsz:   vertex array size in bytes
 * @stride: vertex array element stride (0 means 12 aka 3 * sizeof(float))
 * @xlate:  optional vertex translation matrix to apply before AABB calculation
 *
 * Calculate an AABB box from the vertex array @vx translated by a given
 * matrix and write it to @aabb.
 */
void vertex_array_xlate_aabb_calc(vec3 aabb[2], float *vx, size_t vxsz, size_t stride, mat4x4 *xlate);

/**
 * vertex_array_aabb_calc() - calculate AABB from a vertex array
 * @aabb:   AABB box output
 * @vx:     vertex array
 * @vxsz:   vertex array size in bytes
 * @stride: vertex array element stride (0 means 12 aka 3 * sizeof(float))
 *
 * Calculate an AABB box from the vertex array @vx and write it to @aabb.
 */
static inline void vertex_array_aabb_calc(vec3 aabb[2], float *vx, size_t vxsz, size_t stride)
{
    vertex_array_xlate_aabb_calc(aabb, vx, vxsz, stride, NULL);
}

/**
 * vertex_array_fix_origin() - fix vertex array's origin point
 * @vx:     vertex array
 * @vxsz:   vertex array size
 * @stride: vertex array element stride (0 means 12 aka 3 * sizeof(float))
 * @aabb:   precalculated AABB box
 *
 * Rebuild a vertex array so that the origin point is the center of the
 * bottom side of the precalculated AABB box.
 */
void vertex_array_fix_origin(float *vx, size_t vxsz, size_t stride, vec3 aabb[2]);

/**
 * aabb_point_is_inside() - check if a point is inside of an AABB box
 * @aabb:   AABB box
 * @point:  point to test
 *
 * Return: true if @point is inside @aabb, false otherwise.
 */
static inline bool aabb_point_is_inside(const vec3 aabb[2], const vec3 point)
{
    if (point[0] >= aabb[0][0] && point[0] <= aabb[1][0] &&
        point[1] >= aabb[0][1] && point[1] <= aabb[1][1] &&
        point[2] >= aabb[0][2] && point[2] <= aabb[1][2])
        return true;

    return false;
}

#ifdef _WIN32
#define srand48 srand
#define lrand rand
#define lrand48 rand
static inline double drand48()
{
    return (double)rand() / RAND_MAX;
}
#ifndef thread_local
#define thread_local __declspec(thread)
#endif /* thread_local */
#endif /* _WIN32 */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif /* PATH_MAX */

#ifndef thread_local
#define thread_local _Thread_local
#endif /* thread_local */

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#define xmin(a, b) (min((a), (b)) == (a) ? 0 : 1)
#define min3(a, b, c) (min(a, min(b, c)))
#define xmin3(a, b, c) ({ \
    typeof(a) __x = min3((a), (b), (c)); \
    int __w = 0; \
    if (__x == (b)) __w = 1; else if (__x == (c)) __w = 2; \
    __w; \
})
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#define xmax(a, b) (max((a), (b)) == (a) ? 0 : 1)
#define max3(a, b, c) (max(a, max(b, c)))
#define xmax3(a, b, c) ({ \
    typeof(a) __x = max3((a), (b), (c)); \
    int __w = 0; \
    if (__x == (b)) __w = 1; else if (__x == (c)) __w = 2; \
    __w; \
})

/* Round up a value @x to a power of 2 @y */
#define round_mask_log2(x, y) ((typeof(x))((y) - 1))
#define round_up(x, y) ((((x) - 1) | round_mask_log2(x, y)) + 1)

#define bitmask_field(x, m) \
    ({ \
        typeof(x) __x = (x); \
        typeof(m) __m = (m); \
        typeof(m) off = __builtin_ffs(__m); \
        if (off) { \
            __x = (__x & __m) >> (off - 1); \
        } else { \
            __x = 0; \
        } \
        __x; \
    })

#define CHECK_NVAL(_st, _q, _val) ({ \
    typeof(_val) __x = _q (_st); \
    err_on_cond(__x != (_val), _st != _val, "failed: %ld\n", (long)__x); \
    __x; \
})
#define CHECK_VAL(_st, _val) CHECK_NVAL(_st,, _val)
#define CHECK(_st) CHECK_NVAL(_st, !!, true)
#define CHECK0(_st) CHECK_VAL(_st, 0)

static inline void __noreturn clap_unreachable(void)
{
    unreachable();
}

static inline void str_chomp(char *str)
{
    if (!*str)
        return;

    int i;
    for (i = strlen(str) - 1; isspace(str[i]) && i; i--)
        str[i] = 0;
}

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

static inline bool str_endswith_nocase(const char *str, const char *sfx)
{
    size_t sfxlen = strlen(sfx);
    size_t len = strlen(str);

    if (len < sfxlen)
        return false;

    if (!strncasecmp(str + len - sfxlen, sfx, sfxlen))
        return true;

    return false;
}

static inline const char *str_basename(const char *str)
{
    const char *p = strrchr(str, PATH_DELIM_OS);

    if (p)
        return p + 1;

    return str;
}

/**
 * str_trim_slashes() - remove trailing path delimiters
 * @path: string to trim
 *
 * Remove trailing PATH_DELIM_OS characters while keeping a single leading
 * delimiter intact ("/" stays "/", "//" becomes "/").
 */
static inline void str_trim_slashes(char *path)
{
    if (!path || !*path)    return;

    for (size_t len = strlen(path);
         len > 1 && path[len - 1] == PATH_DELIM_OS;
         path[len - 1] = 0, len--)
        ;
}

/**
 * path_has_parent() - check if path has a parent directory
 * @path: path to check
 *
 * Return: true if @path has a parent directory, false otherwise.
 */
static inline bool path_has_parent(const char *path)
{
    if (!path || !*path)
        return false;

    const char *slash = strrchr(path, PATH_DELIM_OS);
    if (!slash)
        return false;

    if (slash == path && !slash[1])
        return false;

    return true;
}

/**
 * path_parent() - computes the parent path
 * @dst:    buffer to write the parent path to
 * @size:   size of the buffer
 * @path:   path to compute parent for
 *
 * Calculates the parent path of @path and writes it to @dst.
 * Trailing slashes in @path are ignored.
 *
 * Return: CERR_OK on success, CERR_NOT_FOUND if no parent found, error code otherwise.
 */
static inline cerr path_parent(char *dst, size_t size, const char *path)
{
    if (!dst || !size || !path)
        return CERR_INVALID_ARGUMENTS;

    int n = snprintf(dst, size, "%s", path);
    if (n < 0 || (size_t)n >= size)
        return CERR_TOO_LARGE;

    str_trim_slashes(dst);

    char *slash = strrchr(dst, PATH_DELIM_OS);
    if (!slash)
        return CERR_NOT_FOUND;

    if (slash == dst && !slash[1])
        return CERR_NOT_FOUND;

    if (slash == dst)
        slash[1] = 0;
    else
        *slash = 0;

    return CERR_OK;
}

/**
 * path_joinv() - join multiple path components with PATH_DELIM_OS
 * @dst:           output buffer
 * @size:          output buffer size
 * @comps:         NULL-terminated array of const char * components
 *
 * Build a path in @dst from the provided components, inserting a single
 * PATH_DELIM_OS between them. Leading delimiters in subsequent components
 * are skipped to avoid "//". Trailing delimiters are trimmed on the result,
 * except for the root path.
 *
 * Return: CERR_OK on success, CERR_INVALID_ARGUMENTS on bad parameters,
 *         CERR_TOO_LARGE on overflow.
 */
cerr path_joinv(char *dst, size_t size, const char *const comps[]);

/**
 * define path_join - join multiple path components with PATH_DELIM_OS
 * @dst:    output buffer
 * @size:   outbut buffer size
 * @...:    comma-delimited components
 *
 * This is a wrapper around path_joinv() that does the typecasting behind
 * the scenes.
 */
#define path_join(dst, size, ...) \
    path_joinv((dst), (size), (const char *const[]) { __VA_ARGS__, NULL })

struct darray {
    void            *array;
    size_t          elsz;
    size_t          nr_el;
    const char      *mod;
};

#define darray(_type, _name) \
union { \
    _type           *x; \
    struct darray   da; \
} _name;

#define darray_init(_x) do { \
    (_x).da.elsz = sizeof(*(_x).x); \
    (_x).da.nr_el = 0; \
    (_x).da.mod = MODNAME; \
    (_x).x = NULL; \
} while (0)

static inline size_t _darray_count(struct darray *da)
{
    return da->nr_el;
}

/* Return the number of elements in an array */
#define darray_count(_x) _darray_count(&((_x).da))

static inline void *_darray_get(struct darray *da, size_t el)
{
    if (el >= da->nr_el)
        return NULL;
    return da->array + da->elsz * el;
}

/* Get an element of an array */
#define darray_get(_x, _el) ((typeof((_x).x))_darray_get(&((_x).da), (_el)))

void *_darray_resize(struct darray *da, size_t nr_el);

/* Resize an array to a given number of elements */
#define darray_resize(_x, _nr) _darray_resize(&((_x).da), (_nr))

void *_darray_add(struct darray *da);

/* Add an element and return a pointer to it */
#define darray_add(_x) ((typeof((_x).x))_darray_add(&((_x).da)))

void *_darray_insert(struct darray *da, size_t idx);

/* Inserts an element at a given index */
#define darray_insert(_x, _idx) ((typeof((_x).x))_darray_insert(&((_x).da), (_idx)))

void _darray_delete(struct darray *da, size_t idx);

/* Delete an element at a given index */
#define darray_delete(_x, _idx) _darray_delete(&((_x).da), (_idx))

void _darray_clearout(struct darray *da);

/* Free up the array */
#define darray_clearout(_x) _darray_clearout(&((_x).da))

/* A shorthand for getting an element of an array */
#define DA(_x, _idx) (darray_get((_x), _idx))

/* Iterate through the elements of an array */
#define darray_for_each(_el, _da) \
    for (_el = darray_get((_da), 0); \
         _el && _el != &(_da).x[(_da).da.nr_el]; \
         _el++ \
        )

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

unsigned int fletcher32(const unsigned short *data, size_t len);

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

cerr_check hashmap_init(struct hashmap *hm, size_t nr_buckets);
cresp(void) hashmap_find(struct hashmap *hm, unsigned int key);
void hashmap_delete(struct hashmap *hm, unsigned int key);
cerr_check hashmap_insert(struct hashmap *hm, unsigned int key, void *value);
void hashmap_done(struct hashmap *hm);
void hashmap_for_each(struct hashmap *hm, void (*cb)(void *value, void *data), void *data);

struct bitmap {
    unsigned long   *mask;
    size_t          size;
};

void bitmap_init(struct bitmap *b, size_t bits);
void bitmap_done(struct bitmap *b);
void bitmap_set(struct bitmap *b, unsigned int bit);
void bitmap_clear(struct bitmap *b, unsigned int bit);
bool bitmap_is_set(struct bitmap *b, unsigned int bit);
cres(int) bitmap_find_first_set(struct bitmap *b);
cres(int) bitmap_find_first_unset(struct bitmap *b);
cres(int) bitmap_set_lowest(struct bitmap *b);
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

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000l
#endif /* NSEC_PER_SEC */

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
cerr_check exit_cleanup(exit_handler_fn);
void exit_cleanup_run(int status);

#endif /* __CLAP_UTIL_H__ */

