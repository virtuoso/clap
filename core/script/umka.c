// SPDX-License-Identifier: Apache-2.0
//
// Umka-specific FFI: the on_frame() closure entry points, the per-frame tick
// and clap::make_lut(), which calls a scripted colour function back for every
// LUT cell. Unlike the wrappers in ffi-*.c, capturing and calling an Umka
// closure cannot be expressed through the ffi_* accessors, so the frame
// machinery lives in frame.c with the interpreter's own API and this file
// hooks it up. The frame callbacks are this module's private per-script state
// (CLAP_SCRIPT_PRIV), so the script core never sees them.

#include "clap.h"
#include "lut.h"
#include "pipeline.h"
#include "script/ffi.h"
#include "script/frame.h"
#include "script/internal.h"

CLAP_SCRIPT_PRIV(struct list frames)

CLAP_SCRIPT_TYPE("type Frame* = struct { _h: uint }")

/*
 * Register a closure to run every frame, returning a handle that cancel() can
 * later remove. Each deferred call receives a fresh copy of the closure's
 * captured variables, so state that must persist between frames belongs in a
 * module-level variable, not a mutated capture.
 */
CLAP_SCRIPT_FN(ffi_on_frame, "fn on_frame*(cb: fn()): Frame")
void ffi_on_frame(UmkaStackSlot *params, UmkaStackSlot *result)
{
    script *s = ffi_script(result);

    ffi_ret_ptr(params, result, script_frame_capture(s->vm, &s->priv.frames, params, 0));
}

/* Remove a previously registered on_frame() callback; fine from inside one */
CLAP_SCRIPT_METHOD(ffi_frame_cancel, "fn (f: Frame) cancel*()")
void ffi_frame_cancel(UmkaStackSlot *params, UmkaStackSlot *result)
{
    script *s = ffi_script(result);

    script_frame_cancel(&s->priv.frames, ffi_ptr(params, 0));
}

void script_run_frame(script *s)
{
    if (!s || !s->vm || !umkaAlive(s->vm))              return;

    if (!script_frames_run(s->vm, &s->priv.frames))     script_capture_error(s);
}

CLAP_SCRIPT_DONE(script_frames_done)
void script_frames_done(script *s)
{
    script_frames_release(s->vm, &s->priv.frames);
}

/*
 * clap::make_lut() builds a colour-grading LUT from a scripted in->out colour
 * function, so a LUT can be designed and re-rolled live instead of
 * recompiling. The grading function writes its result through an out pointer
 * rather than returning a Vec3, so every argument is a scalar or pointer slot
 * and no struct marshalling is needed. lut_generate_fn() invokes it once per
 * LUT cell; umkaCall() is explicitly reentrant, so calling back into the
 * interpreter from inside this extern is fine and the LUT is ready by the time
 * make_lut returns.
 */
struct lut_closure {
    Umka            *vm;
    UmkaFuncContext fnctx;
    UmkaAny         upvalue;
};

/* Scripted LUTs share one exposure, contrast and side; presets carry their own */
#define LUT_SCRIPT_EXPOSURE 2.0f
#define LUT_SCRIPT_CONTRAST 0.05f
#define LUT_SCRIPT_SIDE     32

static void lut_closure_eval(vec3 color_in, vec3 color_out, void *data)
{
    struct lut_closure *lc = data;
    struct { double x, y, z; } out = { 0, 0, 0 };

    umkaGetParam(lc->fnctx.params, 0)->realVal = color_in[0];
    umkaGetParam(lc->fnctx.params, 1)->realVal = color_in[1];
    umkaGetParam(lc->fnctx.params, 2)->realVal = color_in[2];
    umkaGetParam(lc->fnctx.params, 3)->ptrVal = &out;

    /* Each call consumes a reference to the captured state, as in a frame tick */
    if (lc->upvalue.data)   umkaIncRef(lc->vm, lc->upvalue.data);
    umkaCall(lc->vm, &lc->fnctx);

    color_out[0] = out.x;
    color_out[1] = out.y;
    color_out[2] = out.z;
}

CLAP_SCRIPT_FN(ffi_make_lut, "fn make_lut*(name: str, grade: fn(r, g, b: real, out: ^Vec3)): bool")
void ffi_make_lut(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    struct clap_context *ctx = ffi_ctx(result);
    const char *name = ffi_str(params, 0);
    const UmkaClosure *cb = (const UmkaClosure *)umkaGetParam(params, 1);
    const UmkaType *cb_type = umkaGetParamType(params, 1);

    if (!ctx || !name || !cb_type) {
        ffi_ret_bool(params, result, false);
        return;
    }

    struct lut_closure lc = { .vm = umka, .upvalue = cb->upvalue };
    umkaMakeFuncContext(umka, cb_type, cb->entryOffset, &lc.fnctx);
    *umkaGetUpvalue(lc.fnctx.params) = cb->upvalue;

    /* Rebuilding an existing LUT: note whether it's the one currently applied */
    render_options *ropts = clap_get_render_options(ctx);
    cresp(lut) found = clap_lut_find(ctx, name);
    lut *old = IS_CERR(found) ? NULL : found.val;
    bool was_active = old && ropts->lighting_lut == old;

    cresp(lut) res = lut_generate_fn(clap_get_renderer(ctx), clap_lut_list(ctx), name,
                                     lut_closure_eval, &lc, LUT_SCRIPT_EXPOSURE,
                                     LUT_SCRIPT_CONTRAST, LUT_SCRIPT_SIDE);
    if (IS_CERR(res)) {
        ffi_ret_bool(params, result, false);
        return;
    }

    if (old) {
        /* keep the active LUT pointing at the fresh texture */
        if (was_active)     lut_apply(clap_get_scene(ctx), res.val);
        ref_put_last(old);
    }

    ffi_ret_bool(params, result, true);
}
