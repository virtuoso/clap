/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCRIPT_FFI_H__
#define __CLAP_SCRIPT_FFI_H__

#include <limits.h>
#include "umka_api.h"
#include "script.h"

/*
 * FFI surface markers, scanned by scripts/script-ffi-gen.cmake. All three expand
 * to nothing; the generator turns them into the "clap" Umka module and the
 * umkaAddFunc() registration (see script-ffi-gen.h). A single marker is the sole
 * source of truth for the script-visible declaration and the C binding.
 *
 * CLAP_SCRIPT_TYPE("type Foo* = ...")          - a module type declaration
 * CLAP_SCRIPT_FN(cfunc, "fn name*(sig): ret")  - an exported free function
 * CLAP_SCRIPT_METHOD(cfunc, "fn (r: T) name*(sig): ret")
 *                                              - a private backing external
 *                                                (named after cfunc without the
 *                                                ffi_ prefix) plus the public
 *                                                method that forwards to it
 * CLAP_SCRIPT_PRIV(struct list foo)            - a struct script_priv member, so
 *                                                an FFI module keeps its own
 *                                                per-script state without the
 *                                                core growing a field for it
 * CLAP_SCRIPT_DONE(cfunc)                       - a teardown hook, void cfunc(script *),
 *                                                run when a script is dropped
 */
#define CLAP_SCRIPT_TYPE(decl)
#define CLAP_SCRIPT_FN(cfunc, proto)
#define CLAP_SCRIPT_METHOD(cfunc, proto)
#define CLAP_SCRIPT_PRIV(member)
#define CLAP_SCRIPT_DONE(cfunc)

/*
 * Argument and result accessors. FFI wrappers use only these, never the raw
 * umka API, so the wrappers stay interpreter-agnostic and the umka-specific bits
 * live behind this boundary (and in umka.c). Engine object handles cross the FFI
 * as opaque pointers packed into a single stack slot.
 *
 * umkaGetInstance() must precede the first umkaGetResult(), so a wrapper reads
 * its context and parameters (ffi_ctx / ffi_script / ffi_*) before writing its
 * result (ffi_ret_*).
 */

static inline struct clap_context *ffi_ctx(UmkaStackSlot *result)
{
    return script_context(umkaGetMetadata(umkaGetInstance(result)));
}

static inline script *ffi_script(UmkaStackSlot *result)
{
    return umkaGetMetadata(umkaGetInstance(result));
}

static inline void *ffi_ptr(UmkaStackSlot *params, int idx)
{
    return (void *)(uintptr_t)umkaGetParam(params, idx)->uintVal;
}

static inline const char *ffi_str(UmkaStackSlot *params, int idx)
{
    return umkaGetParam(params, idx)->ptrVal;
}

static inline double ffi_real(UmkaStackSlot *params, int idx)
{
    return umkaGetParam(params, idx)->realVal;
}

static inline bool ffi_bool(UmkaStackSlot *params, int idx)
{
    return umkaGetParam(params, idx)->intVal != 0;
}

/**
 * ffi_int() - read an int argument
 * @params: the call's parameter slots
 * @idx:    the argument's slot
 *
 * Umka's int is 64 bits wide: past either end of int's range, an argument
 * reads as that end instead of wrapping around.
 *
 * Return: the argument, saturated to int's range
 */
static inline int ffi_int(UmkaStackSlot *params, int idx)
{
    int64_t v = umkaGetParam(params, idx)->intVal;

    return v < INT_MIN ? INT_MIN : v > INT_MAX ? INT_MAX : (int)v;
}

static inline void ffi_ret_ptr(UmkaStackSlot *params, UmkaStackSlot *result, void *p)
{
    *(uint64_t *)umkaGetResult(params, result)->ptrVal = (uint64_t)(uintptr_t)p;
}

static inline void ffi_ret_bool(UmkaStackSlot *params, UmkaStackSlot *result, bool b)
{
    umkaGetResult(params, result)->intVal = b;
}

static inline void ffi_ret_str(UmkaStackSlot *params, UmkaStackSlot *result, const char *s)
{
    Umka *umka = umkaGetInstance(result);
    umkaGetResult(params, result)->ptrVal = umkaMakeStr(umka, s ? s : "");
}

static inline void ffi_ret_real(UmkaStackSlot *params, UmkaStackSlot *result, double v)
{
    umkaGetResult(params, result)->realVal = v;
}

static inline void ffi_ret_int(UmkaStackSlot *params, UmkaStackSlot *result, int v)
{
    umkaGetResult(params, result)->intVal = v;
}

static inline void ffi_ret_vec3(UmkaStackSlot *params, UmkaStackSlot *result,
                                float x, float y, float z)
{
    struct { double x, y, z; } *out = umkaGetResult(params, result)->ptrVal;
    out->x = x;
    out->y = y;
    out->z = z;
}

/*
 * Field accessor generators for the many trivial "poke a struct field" wrappers
 * (light/entity/render debugging knobs). Each expands to a full ffi_ wrapper that
 * takes the handle in slot 0; the generator (scripts/script-ffi-gen.cmake) reads
 * the same marker for the Umka binding, so the invocation is the single source of
 * truth. The proto string is consumed by the generator and ignored by the C body.
 *
 *   FFI_GET_FLOAT(ffi_foo_bar, "fn (h: Foo) bar*(): real", foo, bar)
 *   FFI_SET_FLOAT_RANGE(ffi_foo_set_bar, "fn (h: Foo) set_bar*(v: real)", foo, bar, 0.0, 1.0)
 *
 * @cstruct is the bare struct tag (foo -> struct foo *); the handle is a pointer
 * to the object, so a null handle reads a zero / ignores the write.
 */
#define FFI_GET_FLOAT(cfunc, proto, cstruct, field)                                 \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        struct cstruct *o = ffi_ptr(params, 0);                                     \
        ffi_ret_real(params, result, o ? o->field : 0.0);                           \
    }

#define FFI_SET_FLOAT(cfunc, proto, cstruct, field)                                 \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        (void)result;                                                               \
        struct cstruct *o = ffi_ptr(params, 0);                                     \
        if (o)                                                                      \
            o->field = ffi_real(params, 1);                                         \
    }

#define FFI_SET_FLOAT_RANGE(cfunc, proto, cstruct, field, lo, hi)                   \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        (void)result;                                                               \
        struct cstruct *o = ffi_ptr(params, 0);                                     \
        if (o)                                                                      \
            o->field = clampf(ffi_real(params, 1), (lo), (hi));                     \
    }

#define FFI_GET_BOOL(cfunc, proto, cstruct, field)                                  \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        struct cstruct *o = ffi_ptr(params, 0);                                     \
        ffi_ret_bool(params, result, o ? !!o->field : false);                       \
    }

#define FFI_SET_BOOL(cfunc, proto, cstruct, field)                                  \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        (void)result;                                                               \
        struct cstruct *o = ffi_ptr(params, 0);                                     \
        if (o)                                                                      \
            o->field = ffi_bool(params, 1);                                         \
    }

#define FFI_GET_INT(cfunc, proto, cstruct, field)                                   \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        struct cstruct *o = ffi_ptr(params, 0);                                     \
        ffi_ret_int(params, result, o ? o->field : 0);                              \
    }

#define FFI_SET_INT(cfunc, proto, cstruct, field)                                   \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        (void)result;                                                               \
        struct cstruct *o = ffi_ptr(params, 0);                                     \
        if (o)                                                                      \
            o->field = ffi_int(params, 1);                                          \
    }

/* @field is a float[3]/vec3 member; the setter takes three reals, the getter returns Vec3. */
#define FFI_GET_VEC3(cfunc, proto, cstruct, field)                                  \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        struct cstruct *o = ffi_ptr(params, 0);                                     \
        if (o)                                                                      \
            ffi_ret_vec3(params, result, o->field[0], o->field[1], o->field[2]);    \
        else                                                                        \
            ffi_ret_vec3(params, result, 0, 0, 0);                                  \
    }

#define FFI_SET_VEC3(cfunc, proto, cstruct, field)                                  \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        (void)result;                                                               \
        struct cstruct *o = ffi_ptr(params, 0);                                     \
        if (o) {                                                                    \
            o->field[0] = ffi_real(params, 1);                                      \
            o->field[1] = ffi_real(params, 2);                                      \
            o->field[2] = ffi_real(params, 3);                                      \
        }                                                                           \
    }

#endif /* __CLAP_SCRIPT_FFI_H__ */
