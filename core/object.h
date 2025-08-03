/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_OBJECT_H__
#define __CLAP_OBJECT_H__

#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "memory.h"
#include "util.h"
#include "logger.h"
#include "json.h" /* XXX: factor out ser/deser code */
#include "refclasses.h"

struct ref;
typedef cerr (*make_t)(struct ref *ref, void *opts);
typedef void (*drop_t)(struct ref *ref);

#define _REF_STATIC	    (-1)
#define _REF_EMBEDDED	(-2)

/**
 * struct ref_class - class descriptor
 * @entry:      on global list of classes; not thread-safe at the moment
 * @name:       class name
 * @make:       constructor
 * @drop:       destructor
 * @size:       object size
 * @offset:     offset of struct ref in an object (see ref_obj())
 * @nr_active:  nr of active objects; not thread-safe
 *
 * A very simple type descriptor with a constructor, destructor and some
 * bits of auxiliary info about the type.
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

/**
 * define REFCLASS_NAME - extrapolate a refclass name from struct name
 * @struct_name:    name of the struct for which a refclass is inferred
 *
 * The macro serves 2 purposes: to avoid repetition and to make potential
 * refactoring easier.
 */
#define REFCLASS_NAME(struct_name) ref_class_ ## struct_name

/**
 * define DEFINE_REFCLASS_MAKE_DROP - define a refclass (in a compilation unit)
 * @struct_name:    name of the struct for which a refclass is being defined
 * @makefn:         refclass constructor
 * @dropfn:         refclass destructor
 *
 * This defines and initializes the refclass object for a given structure, its
 * struct ref * getter and a cresp(struct_name) type. This goes in a compilation
 * unit where said struct is defined and its constructor/destructor and the rest
 * of its functions are implemented, to allow the structure itself to remain
 * local to the compilation unit. For header files, use DECLARE_REFCLASS().
 */
#define DEFINE_REFCLASS_MAKE_DROP(struct_name, makefn, dropfn) \
    struct ref_class REFCLASS_NAME(struct_name) = { \
        .name   = __stringify(struct struct_name), \
        .make   = makefn, \
        .drop   = dropfn, \
        .size   = sizeof(struct struct_name), \
        .offset = offsetof(struct struct_name, ref), \
        .entry  = EMPTY_LIST(REFCLASS_NAME(struct_name).entry), \
    }; \
    struct ref *struct_name ## _ref(void *_obj) \
    { \
        struct struct_name *obj = _obj; \
        return &obj->ref; \
    } \
    typedef struct cresp(struct_name) cresp(struct_name)

/**
 * define rc_init_opts - extrapolate constructor options type name from struct name
 * @struct_name:    struct name
 *
 * This again serves 2 purposes: to avoid repetition and make refactoring
 * slightly easier. And requires less typing. The value of this macro is the
 * name of the structure that's passed from ref_new*() to this structure's
 * refclass makefn(), that is, constructor options.
 */
#define rc_init_opts(struct_name) struct_name ## _init_options

/**
 * define DEFINE_REFCLASS_INIT_OPTIONS - define constructor options for struct name
 * @struct_name:    struct name, for which constructor options are being defined
 * @args:           an open-ended list of constructor options
 *
 * See &struct model3dtx_init_options for an example. @args are of the form
 * `.field0 = initializer0, .field1 = initializer1`.
 */
#define DEFINE_REFCLASS_INIT_OPTIONS(struct_name, args...) \
    typedef struct rc_init_opts(struct_name) { args } rc_init_opts(struct_name)

/**
 * define DEFINE_REFCLASS_DROP - define a refclass with just a drop method
 * @struct_name:    name of the struct for which a refclass is being defined
 * @dropfn:         refclass destructor
 *
 * Same as DEFINE_REFCLASS_MAKE_DROP(), but without makefn. Should not be used
 * in the wild; its more compact version DEFINE_REFCLASS() should also rarely
 * be used.
 */
#define DEFINE_REFCLASS_DROP(struct_name, dropfn) \
    DEFINE_REFCLASS_MAKE_DROP(struct_name, NULL, dropfn)

/**
 * define DEFINE_REFCLASS - define a refclass with just a drop method named <struct_name>_drop()
 * @struct_name:    name of the struct for which a refclass is being defined
 *
 * Wrapper around DEFINE_REFCLASS_DROP() with dropfn inferred from struct_name
 * as <struct_name>_drop().
 */
#define DEFINE_REFCLASS(struct_name)   DEFINE_REFCLASS_DROP(struct_name, struct_name ## _drop)

/**
 * define DEFINE_REFCLASS2 - define a refclass with drop and make methods
 * @struct_name:    name of the struct for which a refclass is being defined
 *
 * Wrapper around DEFINE_REFCLASS_MAKE_DROP() with makefn and dropfn inferred
 * from struct_name as <struct_name>_make() and <struct_name>_drop(). Should
 * be the most common variant.
 */
#define DEFINE_REFCLASS2(struct_name) \
    DEFINE_REFCLASS_MAKE_DROP(struct_name, struct_name ## _make, struct_name ## _drop)

/**
 * define DECLARE_REFCLASS - declare a refclass for a struct name (in a header file)
 * @struct_name:    name of the struct for which a refclass is being declared
 *
 * Use in a header file. Declare stuff that DEFINE_REFCLASS_MAKE_DROP() defined
 * for the use outside of its compilation unit.
 */
#define DECLARE_REFCLASS(struct_name) \
    extern struct ref_class REFCLASS_NAME(struct_name); \
    extern struct ref *struct_name ## _ref(void *obj); \
    cresp_struct_ret(struct_name)

/**
 * struct ref - reference counting
 * @refclass:   class aka type descriptor
 * @count:      the number of active references
 * @consume:    set by ref_pass() so that next ref_get() gets caller's reference
 *
 * Embeddable structure that turns other structures into refcounted types with
 * optional constructor (make), destructor (drop) and some accounting.
 */
struct ref {
    struct ref_class    *refclass;
    int		count;
    bool    consume;
};

void ref_class_add(struct ref *ref);
void ref_class_unuse(struct ref *ref);
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
        mem_free(ref_obj(__ref)); \
    } \
} while (0)

#define REF_STATIC(_name) { .count = _REF_STATIC, .refclass = &REFCLASS_NAME(_name) }

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

#define ref_get(t) _ref_get(t, __obj_ref(t))

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

#define ref_put(obj) ref_put_ref(__obj_ref(obj))

#define ref_put_passed_ref(r) do { \
        if ((r)->consume) { \
            (r)->consume = false; \
            ref_dbg("ref_put_passed_ref(%s): %d\n", _ref_name(r), (r)->count - 1); \
            _ref_put(r); \
        } \
    } while (0)

#define ref_put_passed(obj) ref_put_passed_ref(&(obj)->ref)

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

/* Dynamically allocate an object and return cresp(struct_name) */
#define ref_new_checked(struct_name, args...) ({ \
    struct ref_class *__rc = &REFCLASS_NAME(struct_name); \
    struct struct_name *__v = mem_alloc(__rc->size, .zero = 1); \
    cresp(struct_name) __res = cresp_val(struct_name, __v); \
    if (__v) { \
        struct ref *__ref = (void *)__v + __rc->offset; \
        __ref->refclass = __rc; \
        ref_init(__ref); \
        if (__rc->make) { \
            cerr err = __rc->make(__ref, &(rc_init_opts(struct_name)){ args }); \
            if (IS_CERR(err)) { \
                ref_class_unuse(__ref); \
                mem_free(__v); \
                __res = cresp_error_cerr(struct_name, err); \
            } \
        } \
    } else { \
        __res = cresp_error(struct_name, CERR_NOMEM); \
    } \
    __res; \
})

/* Dynamically allocate an object and return a pointer or NULL */
#define ref_new(struct_name, args...) ({ \
    cresp(struct_name) __res = ref_new_checked(struct_name, args); \
    struct struct_name *__v = NULL; \
    if (!IS_CERR(__res)) \
        __v = __res.val; \
    __v; \
})

/* Initialize a static/embedded object and return cresp(struct_name) */
#define ref_embed(struct_name, _obj, args...) ({ \
    struct ref_class *__rc = &REFCLASS_NAME(struct_name); \
    typeof (_obj) __v = (_obj); \
    memset(__v, 0, __rc->size); \
    __v->ref.refclass = __rc; \
    _ref_embed(&__v->ref); \
    cerr err = __rc->make ? \
        __rc->make(&__v->ref, &(rc_init_opts(struct_name)){ args }) : \
        CERR_OK; \
    err; \
})

/*
 * Give the ownership to a callee
 */
#define ref_pass(obj) ({ \
    typeof (obj) __obj = (obj); \
    obj = NULL; \
    struct ref *__ref = __obj_ref(__obj); \
    __ref->consume = true; \
    __obj; \
})

#if !defined(CONFIG_FINAL) && !defined(CLAP_TESTS)
void memory_debug(void);
#else
static inline void memory_debug(void) {}
#endif /* CONFIG_FINAL || CLAP_TESTS */

#endif /* __CLAP_OBJECT_H__ */
