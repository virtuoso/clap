/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCRIPT_FFI_H__
#define __CLAP_SCRIPT_FFI_H__

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

static inline void ffi_ret_vec3(UmkaStackSlot *params, UmkaStackSlot *result,
                                float x, float y, float z)
{
    struct { double x, y, z; } *out = umkaGetResult(params, result)->ptrVal;
    out->x = x;
    out->y = y;
    out->z = z;
}

#endif /* __CLAP_SCRIPT_FFI_H__ */
