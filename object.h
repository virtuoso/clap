#ifndef __CLAP_OBJECT_H__
#define __CLAP_OBJECT_H__

#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "util.h"
#include "logger.h"
#include "json.h" /* XXX: factor out ser/deser code */

struct ref;
typedef void (*drop_t)(struct ref *ref);

#define _REF_STATIC	(-1)
struct ref {
    const char *name;
    drop_t	drop;
    int		count;
    size_t  size;
};

static inline void ref_init(struct ref *ref, drop_t drop)
{
    ref->count = 1;
    ref->drop = drop;
}

#define REF_STATIC { .count = _REF_STATIC, .drop = NULL }

static inline bool __ref_get(struct ref *ref)
{
    if (ref->count) {
        ref->count++;
        return true;
    }

    return false;
}

//#define DEBUG_REF 1
#ifdef DEBUG_REF
#define ref_dbg(fmt, args...) dbg(fmt, ## args)
#else
#define ref_dbg(args...)
#endif
#define _ref_get(t, r) ({ \
    typeof(t)__o = NULL; \
    if (__ref_get(r)) \
        __o = (t); \
    ref_dbg("ref_get(%s): %d\n", (r)->name, (r)->count); \
    __o; \
})

#define ref_get(t) _ref_get(t, &t->ref)

static inline void _ref_put(struct ref *ref)
{
    if (!--ref->count)
        ref->drop(ref);
}

#define ref_put(r) do { \
        ref_dbg("ref_put(%s): %d\n", (r)->name, (r)->count - 1); \
        _ref_put(r); \
    } while (0)

#define ref_put_last(r) do { \
        ref_dbg("ref_put_last(%s): %d\n", (r)->name, (r)->count - 1); \
        err_on(!!--(r)->count, "'%s': %d\n", (r)->name, (r)->count); \
        (r)->drop((r)); \
    } while (0)

#define ref_assert(r, c) do { \
        err_on((r)->count != (c), "'%s' expected %d, got %d\n", \
            (r)->name, (c), (r)->count); \
    } while (0)

#define ref_only(r) ref_assert(r, 1)

#define ref_new(t, r, d) ({ \
    size_t _tsz = sizeof(t); \
    t *__v = malloc(_tsz); \
    if (__v) { \
        memset(__v, 0, _tsz); \
        ref_init(&__v->r, d); \
        __v->r.name = # t; \
        __v->r.size = _tsz; \
    } \
    __v; \
})

void cleanup__ref(struct ref **ref);
struct object;
typedef struct object Object;
void cleanup__Objectp(Object **object);

/*
 * 0. Class is an instance of struct class
 *    struct class Class = { .name = "Class", .make = class_make, ..., .size = sizeof(struct class) };
 * 1. Inheritance by embedding
 *    struct blah_type { // type definition
 *        const struct class  super; // make sure blah doesn't poke inside @class
 *        int                 (*blah_get)(void *self);
 *        void                (*blah_set)(void *self, int blah);
 *    } blah = { .blah_get = blah_get, .blah_set = blah_set };
 * 2. Instantiation by keeping a pointer to the type:
 *    struct blah {
 *        struct blah_type    *type;
 *        int                 blah_data;
 *    };
 * 2. How do we know if an object implements an interface?
 * 2.1. Do we really want to do that?
 *
 * struct class embedded into a larger object structure,
 *            defining a class aka type
 *  + drop(): destructor
 *  + clone()
 *  + copy()
 *
 *  +-> clone(): clone an existing object (copy)
 *  +-> drop():  destroy
 *
 */
struct class {
/* or:

struct object_type { // for struct object
struct type {

    or
struct object -> struct base
struct class  -> struct base_type

 */
    const char	*name;
    size_t		size;

    void        *(*make)(void *self, va_list *args);
    void        *(*drop)(void *self);
    void        *(*clone)(void *self);
    int         (*cmp)(void *self, void *other);
    int         (*serialize)(void *self, JsonNode *root);
    void        *(*deserialize)(void *self, JsonNode *root);
};

extern struct class class;

void print_each_class(void);
struct class *class_find(const char *name);

void *make(void *type, ...);
void *_make(int argc, void *type, ...);
#define MAKE(t, ...) ({ _make(sizeof({__VA_ARGS__}/sizeof(int)), t, __VA_ARGS__)})
void drop(void *object);
int cmp(void *object, void *other);
int serialize(void *object, JsonNode *root);
void *deserialize(void *object, JsonNode *root);

#ifdef __EMSCRIPTEN__
#define DECLARE_CLASS(c)
#else
#define DECLARE_CLASS(c)                                                     \
    __attribute__((section("ClassList"))) void *__##c##_desc = &c;
#endif

#endif /* __CLAP_OBJECT_H__ */

