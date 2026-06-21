// SPDX-License-Identifier: Apache-2.0
//
// Umka scripting subsystem core: the script object, its lifecycle and the
// per-frame tick. The FFI surface is assembled from the markers in the ffi-*.c
// wrappers (see ffi.h) by scripts/script-ffi-gen.cmake, pulled in below as the
// "clap" module source and its registration. Engine object handles are passed
// to scripts as opaque pointers, valid only while the underlying object lives.

#include "logger.h"
#include "memory.h"
#include "script/internal.h"

/* Generated: clap_script_um[] (the "clap" module) and clap_script_ffi_register(). */
#include "script-ffi-gen.h"

/*
 * Copy the interpreter's current error into s->error and log it. Only valid
 * after a successful umkaInit(), which sets up the error report.
 */
void script_capture_error(script *s)
{
    UmkaError *e = umkaGetError(s->vm);

    mem_free(s->error);
    s->error = NULL;
    (void)CRES_RET(mem_asprintf(&s->error, "%s (%d): %s", e->fileName ? e->fileName : s->name,
                                e->line, e->msg ? e->msg : "unknown error"),
                   s->error = NULL);

    err("script '%s': %s\n", s->name, s->error ? s->error : "compile/runtime error");
}

cerr script_compile(script *s)
{
    if (!s || !s->vm)   return CERR_INVALID_ARGUMENTS;
    if (s->compiled)    return CERR_OK;

    if (!umkaCompile(s->vm)) {
        script_capture_error(s);
        return CERR_PARSE_FAILED;
    }

    s->compiled = true;
    return CERR_OK;
}

cerr script_run(script *s)
{
    if (!s || !s->compiled)     return CERR_INVALID_OPERATION;

    if (umkaRun(s->vm) != 0) {
        script_capture_error(s);
        return CERR_INVALID_OPERATION;
    }

    return CERR_OK;
}

const char *script_error(script *s)
{
    return s ? s->error : NULL;
}

bool script_persistent(script *s)
{
    return s && script_priv_persistent(s);
}

struct clap_context *script_context(script *s)
{
    return s ? s->ctx : NULL;
}

/****************************************************************************
 * Refcounting
 ****************************************************************************/

static cerr script_make(struct ref *ref, void *_opts)
{
    rc_init_opts(script) *opts = _opts;

    script *s = container_of(ref, struct script, ref);
    script_priv_init(s);

    if (!opts->ctx || !opts->source)    return CERR_INVALID_ARGUMENTS;

    s->ctx = opts->ctx;
    CRES_RET_CERR(mem_asprintf(&s->name, "%s", opts->name ? opts->name : "script"));
    CRES_RET_CERR(mem_asprintf(&s->source, "%s", opts->source));

    s->vm = umkaAlloc();
    if (!s->vm)                         return CERR_NOMEM;

    /*
     * umkaInit() must precede the first umkaGetError()/umkaFree(); doing it here
     * keeps the VM droppable for the whole object lifetime. It only fails on bad
     * parameters (never on script syntax -- that surfaces at umkaCompile()), so a
     * generic message is enough.
     */
    if (!umkaInit(s->vm, s->name, s->source, 1024 * 1024, NULL, 0, NULL, false, false, NULL)) {
        err("script '%s': interpreter initialization failed\n", s->name);
        return CERR_INVALID_OPERATION;
    }

    umkaSetMetadata(s->vm, s);
    clap_script_ffi_register(s->vm);

    if (!umkaAddModule(s->vm, "clap.um", clap_script_um)) {
        script_capture_error(s);
        return CERR_INVALID_OPERATION;
    }

    return CERR_OK;
}

static void script_drop(struct ref *ref)
{
    script *s = container_of(ref, struct script, ref);

    /* Let each FFI module release its per-script state before the VM goes. */
    script_priv_done(s);

    if (s->vm)  umkaFree(s->vm);
    mem_free(s->name);
    mem_free(s->source);
    mem_free(s->error);
}

DEFINE_REFCLASS2(script);
DEFINE_CLEANUP(script, if (*p) ref_put(*p));

cresp(script) script_new(struct clap_context *ctx, const char *name, const char *source)
{
    return ref_new_checked(script, .ctx = ctx, .name = name, .source = source);
}

cresp(script) script_load(struct clap_context *ctx, const char *name, const char *source)
{
    LOCAL_SET(script, s) = CRES_RET_T(script_new(ctx, name, source), script);

    CERR_RET_T(script_compile(s), script);
    CERR_RET_T(script_run(s), script);

    return cresp_val(script, NOCU(s));
}
