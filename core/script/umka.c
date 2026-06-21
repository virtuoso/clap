// SPDX-License-Identifier: Apache-2.0
//
// Umka-specific FFI: the on_frame() closure entry points and the per-frame
// tick. Unlike the wrappers in ffi-*.c, capturing and calling an Umka closure
// cannot be expressed through the ffi_* accessors, so the machinery lives in
// frame.c with the interpreter's own API and this file hooks it up. The frame
// callbacks are this module's private per-script state (CLAP_SCRIPT_PRIV), so
// the script core never sees them.

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
