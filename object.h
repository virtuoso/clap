#ifndef __CLAP_OBJECT_H__
#define __CLAP_OBJECT_H__

#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "util.h"

struct ref;
typedef void (*drop_t)(struct ref *ref);

#define _REF_STATIC	(-1)
struct ref {
    int		count;
    drop_t	drop;
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

#define _ref_get(t, r) ({ \
    typeof(t)__o = NULL; \
    if (__ref_get(r)) \
        __o = (t); \
    __o; \
})

#define ref_get(t) _ref_get(t, &t->ref)

static inline void ref_put(struct ref *ref)
{
    if (!--ref->count)
        ref->drop(ref);
}

#define ref_new(t, r, d) ({ \
    t *__v = malloc(sizeof(t)); \
    if (__v) { \
        memset(__v, 0, sizeof(t)); \
        ref_init(&__v->r, d); \
    } \
    __v; \
})

void cleanup__ref(struct ref **ref);

/* XXX --- scratch vmethod
 * vtable is an interesting topic
 * We want to be able to translate method name into table index
 * TYPE(my_type, METHODS())
 * METHOD(my_type, drop)
 */

struct obj;
typedef int (*method_t)(struct obj *self, void *param);

struct vmethod {
    method_t        method;
    struct vmethod  *next;
};

/*
 * 1. Inheritance by embedding
 * 2. How do we know if an object implements an interface?
 * 2.1. Do we really want to do that?
 *
 * struct obj embedded into a larger object structure,
 *            defining a class aka type
 *  + drop(): destructor
 *  + clone()
 *  + copy()
 *
 *  +-> clone(): clone an existing object (copy)
 *  +-> drop():  destroy
 *
 * class (static object)
 *  +-> new():   create an object (constructor, factory)
 *  +-> init():  initialize type (type constructor, self)
 *  +-> inherit(from_class): initialize an inherited type
 */
struct obj {
    const char	*name;
    size_t		size;

    struct ref	ref;

    struct obj	*parent;
};

#define ref_trait(s) \
    extern void __trait_ref_ ## s ## _drop(struct s);

#define DECLARE_TRAIT(trait, s) \
    trait ## _trait(s);

DECLARE_TRAIT(ref, obj);

extern struct obj *__obj_new(const char *name, size_t size);
#define obj_new(o) ({ \
    struct o *o = malloc(sizeof(struct o)); \
    struct obj *obj = container_of(o, struct o, obj); \
    obj_init(obj, # o); \
})

#endif /* __CLAP_OBJECT_H__ */

