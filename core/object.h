/* SPDX-License-Identifier: Apache-2.0 */
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
typedef int (*make_t)(struct ref *ref);
typedef void (*drop_t)(struct ref *ref);

#define _REF_STATIC	    (-1)
#define _REF_EMBEDDED	(-2)

/*
 * Class descriptor
 * @entry:      on global list of classes; not thread-safe at the moment
 * @name:       class name
 * @make:       constructor
 * @drop:       destructor
 * @size:       object size
 * @offset:     offset of struct ref in an object (see ref_obj())
 * @nr_active:  nr of active objects; not thread-safe
 */
struct ref_class {
    struct list     entry;
    const char      *name;
    make_t          make;
    drop_t          drop;
    size_t          size;
    size_t          offset;
    unsigned long   nr_active;
};

#define REFCLASS_NAME(struct_name) ref_class_ ## struct_name
#define DECLARE_REFCLASS_MAKE_DROP(struct_name, makefn, dropfn) \
struct ref_class ref_class_ ## struct_name = { \
    .name   = __stringify(struct struct_name), \
    .make   = makefn, \
    .drop   = dropfn, \
    .size   = sizeof(struct struct_name), \
    .offset = offsetof(struct struct_name, ref), \
    .entry  = EMPTY_LIST(REFCLASS_NAME(struct_name).entry), \
}
#define DECLARE_REFCLASS_DROP(struct_name, dropfn) \
    DECLARE_REFCLASS_MAKE_DROP(struct_name, NULL, dropfn)
#define DECLARE_REFCLASS(struct_name)   DECLARE_REFCLASS_DROP(struct_name, struct_name ## _drop)
#define DECLARE_REFCLASS2(struct_name) \
    DECLARE_REFCLASS_MAKE_DROP(struct_name, struct_name ## _make, struct_name ## _drop)

/*
 * Reference counting
 * @refclass:   class aka type descriptor
 * @count:      the number of active references
 * @consume:    set by ref_pass() so that next ref_get() gets caller's reference
 */
struct ref {
    struct ref_class    *refclass;
    int		count;
    bool    consume;
};

void ref_class_add(struct ref *ref);
const char *ref_classes_get_string(void);

static inline const char *_ref_name(struct ref *ref)
{
    return ref->refclass->name;
}

#define ref_name(_x) _ref_name(&(_x)->ref)

static inline void *ref_obj(struct ref *ref)
{
    return (void *)ref - ref->refclass->offset;
}

static inline void ref_init(struct ref *ref)
{
    ref->count = 1;
    ref_class_add(ref);
}

static inline void _ref_embed(struct ref *ref)
{
    ref->count = _REF_EMBEDDED;
    ref_class_add(ref);
}

static inline bool ref_is_static(struct ref *ref)
{
    return ref->count == _REF_STATIC || ref->count == _REF_EMBEDDED;
}

#define ref_free(_ref) do { \
    struct ref *__ref = (_ref); \
    if (!ref_is_static(__ref)) { \
        err_on(__ref->count != 0, "freeing object '%s' with refcount %d\n", \
               __ref->refclass->name, __ref->count); \
        free(ref_obj(__ref)); \
    } \
} while (0)

#define REF_STATIC { .count = _REF_STATIC, .drop = NULL }

static inline bool __ref_get(struct ref *ref)
{
    if (ref_is_static(ref)) {
        err("ref_get() on static object %s\n", _ref_name(ref));
        return false;
    }

    if (ref->count) {
        if (ref->consume)
            ref->consume = false;
        else
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
    ref_dbg("ref_get(%s): %d\n", _ref_name(r), (r)->count); \
    __o; \
})

#define ref_get(t) _ref_get(t, &t->ref)

void _ref_drop(struct ref *ref);

static inline void _ref_put(struct ref *ref)
{
    if (ref_is_static(ref)) {
        err("ref_put() on static object %s\n", _ref_name(ref));
        return;
    }

    if (!--ref->count)
        _ref_drop(ref);
}

#define ref_put_ref(r) do { \
        ref_dbg("ref_put_ref(%s): %d\n", _ref_name(r), (r)->count - 1); \
        _ref_put(r); \
    } while (0)

#define ref_put(obj) ref_put_ref(&(obj)->ref)

#define ref_put_last_ref(r) do { \
        ref_dbg("ref_put_last_ref(%s): %d\n", _ref_name(r), (r)->count - 1); \
        err_on(!!--(r)->count, "'%s': %d\n", _ref_name(r), (r)->count); \
        _ref_drop((r)); \
    } while (0)

#define ref_put_last(obj) ref_put_last_ref(&(obj)->ref)

/*
 * Expect object to have reference count @c
 */
#define ref_assert(r, c) do { \
        err_on((r)->count != (c), "'%s' expected %d, got %d\n", \
            _ref_name(r), (c), (r)->count); \
    } while (0)

/*
 * Annotate object to have exactly one reference
 */
#define ref_only_ref(r) ref_assert(r, 1)
#define ref_only(obj) ref_only_ref(&(obj)->ref)

/*
 * Annotate object to have more than just the current reference
 */
#define ref_shared_ref(r) do { \
        err_on((r)->count == 1 && (r)->consume, "'%s' expected shared\n", \
            _ref_name(r)); \
    } while (0)
#define ref_shared(obj) ref_shared_ref(&(obj)->ref)

/*
 * Dynamically allocate an object
 */
#define ref_new(struct_name) ({ \
    struct ref_class *__rc = &ref_class_ ## struct_name; \
    struct struct_name *__v = malloc(__rc->size); \
    if (__v) { \
        memset(__v, 0, __rc->size); \
        __v->ref.refclass = __rc; \
        ref_init(&__v->ref); \
        if (__rc->make) __rc->make(&__v->ref); \
    } \
    __v; \
})

/*
 * Initialize a static/embedded object
 */
#define ref_embed(struct_name, _obj) ({ \
    struct ref_class *__rc = &ref_class_ ## struct_name; \
    typeof (_obj) __v = (_obj); \
    memset(__v, 0, __rc->size); \
    __v->ref.refclass = __rc; \
    _ref_embed(&__v->ref); \
    if (__rc->make) __rc->make(&__v->ref); \
})

/*
 * Give the ownership to a callee
 */
#define ref_pass(obj) ({ \
    typeof (obj) __obj = (obj); \
    obj = NULL; \
    __obj->ref.consume = true; \
    __obj; \
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

#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
#define DECLARE_CLASS(c)
#else
#define DECLARE_CLASS(c)                                                     \
    __attribute__((section("ClassList"))) void *__##c##_desc = &c;
#endif

#endif /* __CLAP_OBJECT_H__ */

