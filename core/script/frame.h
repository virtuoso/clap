/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCRIPT_FRAME_H__
#define __CLAP_SCRIPT_FRAME_H__

#include <stdbool.h>
#include "umka_api.h"
#include "util.h"

/*
 * Per-frame callbacks captured from scripts: a list of closures that
 * script_frames_run() calls once per frame. Kept apart from the FFI wrappers
 * so that the mechanism links without the engine, for the tests.
 */

struct script_frame;

/**
 * script_frame_capture() - capture a closure passed to an extern as a frame callback
 * @vm:     the interpreter the closure belongs to
 * @frames: list the callback joins
 * @params: the extern's parameter slots
 * @idx:    index of the closure parameter
 *
 * Pins the closure's captured state so it outlives the extern call.
 *
 * Return: the new callback, or NULL when out of memory.
 */
struct script_frame *script_frame_capture(Umka *vm, struct list *frames, UmkaStackSlot *params,
                                          int idx);

/**
 * script_frame_cancel() - stop a frame callback
 * @frames: the list the callback is on
 * @frame:  the callback to cancel
 *
 * Safe to call from inside any callback, its own included: the entry stays
 * on the list, skipped, until the next script_frames_run() or
 * script_frames_release() reaps it.
 *
 * Return: true if @frame was on the list, false otherwise.
 */
bool script_frame_cancel(struct list *frames, struct script_frame *frame);

/**
 * script_frames_run() - call every live frame callback once
 * @vm:     the interpreter
 * @frames: the callbacks
 *
 * Return: false if the interpreter died in one of the callbacks, true
 * otherwise.
 */
bool script_frames_run(Umka *vm, struct list *frames);

/**
 * script_frames_release() - drop every frame callback
 * @vm:     the interpreter; the captured state is released while it is alive
 * @frames: the callbacks
 */
void script_frames_release(Umka *vm, struct list *frames);

#endif /* __CLAP_SCRIPT_FRAME_H__ */
