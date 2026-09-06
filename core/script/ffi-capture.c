// SPDX-License-Identifier: Apache-2.0
//
// FFI over the screen recorder: arm the pipeline for capture and shoot frames
// into OpenEXR files of the linear scene colour, the way the "screen recorder"
// debug window does, so that captures run from a script -- in tests, over ssh
// -- without the debug UI. The directory and the cap a script sets last for
// the session and leave the settings alone.
//
// A shot is written once the readback delivers it, a few frames later: from
// then on the cap counts it, and frames_captured() with it, while one delivered
// past the cap is dropped. Ending the session with quit() lets the frames
// handed to the writers finish. Final builds have no recorder, and sessions
// without graphics nothing to capture: nothing arms there, and every shot fails.

#include "screen-record.h"
#include "script/ffi.h"

/* Render the screen pass for capture (or stop); the pipeline is rebuilt at the end of the frame. */
CLAP_SCRIPT_FN(ffi_capture_arm, "fn capture_arm*(on: bool)")
void ffi_capture_arm(UmkaStackSlot *params, UmkaStackSlot *result)
{
    screen_record_arm(ffi_ctx(result), ffi_bool(params, 0));
}

/* Whether the rebuilt pipeline is in place, so that screenshot() can capture. */
CLAP_SCRIPT_FN(ffi_capture_is_armed, "fn capture_is_armed*(): bool")
void ffi_capture_is_armed(UmkaStackSlot *params, UmkaStackSlot *result)
{
    ffi_ret_bool(params, result, screen_record_is_armed(ffi_ctx(result)));
}

/* Capture this frame into the capture directory; false if no capture could be armed for it. */
CLAP_SCRIPT_FN(ffi_screenshot, "fn screenshot*(): bool")
void ffi_screenshot(UmkaStackSlot *params, UmkaStackSlot *result)
{
    cerr err = screen_record_shoot(ffi_ctx(result));
    ffi_ret_bool(params, result, !IS_CERR(err));
}

/*
 * Capture into an existing directory from now on: the frames handed to the
 * writers from here go there. False if it can't be opened, and in browser
 * builds, which capture into /screenshots only.
 */
CLAP_SCRIPT_FN(ffi_set_screenshot_dir, "fn set_screenshot_dir*(dir: str): bool")
void ffi_set_screenshot_dir(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const char *dir = ffi_str(params, 0);
    cerr err = screen_record_set_dir(ffi_ctx(result), dir ? dir : "");
    ffi_ret_bool(params, result, !IS_CERR(err));
}

/* Cap the frames the session captures, as the window's cap field does; false for a cap below 1. */
CLAP_SCRIPT_FN(ffi_set_capture_max_frames, "fn set_capture_max_frames*(max_frames: int): bool")
void ffi_set_capture_max_frames(UmkaStackSlot *params, UmkaStackSlot *result)
{
    cerr err = screen_record_set_max_frames(ffi_int(params, 0));
    ffi_ret_bool(params, result, !IS_CERR(err));
}

/* The frames handed to the writers so far this session: the ones the cap counts. */
CLAP_SCRIPT_FN(ffi_frames_captured, "fn frames_captured*(): int")
void ffi_frames_captured(UmkaStackSlot *params, UmkaStackSlot *result)
{
    ffi_ret_int(params, result, screen_record_nr_captured());
}
