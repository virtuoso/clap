// SPDX-License-Identifier: Apache-2.0
//
// FFI for driving the running session headlessly: end or restart the process,
// and inject input so a script can walk the controlled character around (handy
// for physics / kinematic controller work) without a human at the keyboard.
//
// Input is synthesised as MST_KEYBOARD messages -- the scene's input filter
// drops other sources unless they match the active gamepad, and MST_KEYBOARD is
// the catch-all "primary input" the character controller consumes. Directions
// use the controller's press(1)/release(2) held-state convention, so a movement
// set once persists until changed; jump and dash are one-shot edges.

#include <string.h>

#include "clap.h"
#include "display.h"
#include "input.h"
#include "messagebus.h"
#include "script/ffi.h"

static struct message_source script_source = {
    .name = "script",
    .desc = "clap scripting input",
    .type = MST_KEYBOARD,
};

/*
 * End the process cleanly (unwinds the main loop, runs clap_done()). Named
 * quit() rather than exit() to avoid Umka's built-in exit(), which is an abrupt
 * libc exit that skips clap's shutdown.
 */
CLAP_SCRIPT_FN(ffi_quit, "fn quit*()")
void ffi_quit(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)params;
    (void)result;
    display_request_exit();
}

/* Restart the process in place (clap_done() then re-exec); does not return. */
CLAP_SCRIPT_FN(ffi_restart, "fn restart*()")
void ffi_restart(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)params;
    clap_restart(ffi_ctx(result));
}

/*
 * Hold a movement direction on the controlled character (or the free camera if
 * nothing is controlled). The state persists, so call it again with different
 * flags to change course and with all-false to stop.
 */
CLAP_SCRIPT_FN(ffi_input_move, "fn input_move*(fwd, back, left, right: bool)")
void ffi_input_move(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct message_input mi;
    memset(&mi, 0, sizeof(mi));
    mi.up    = ffi_bool(params, 0) ? 1 : 2;
    mi.down  = ffi_bool(params, 1) ? 1 : 2;
    mi.left  = ffi_bool(params, 2) ? 1 : 2;
    mi.right = ffi_bool(params, 3) ? 1 : 2;
    message_input_send(ffi_ctx(result), &mi, &script_source);
}

/* Trigger one jump (ignored while already airborne). */
CLAP_SCRIPT_FN(ffi_input_jump, "fn input_jump*()")
void ffi_input_jump(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)params;
    struct message_input mi;
    memset(&mi, 0, sizeof(mi));
    mi.space = 1;
    message_input_send(ffi_ctx(result), &mi, &script_source);
}

/* Open or close the in-game menu, as the Escape key does; menu navigation then goes through input_move(). */
CLAP_SCRIPT_FN(ffi_input_menu, "fn input_menu*()")
void ffi_input_menu(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct message_input mi;
    memset(&mi, 0, sizeof(mi));
    mi.menu_toggle = 1;
    message_input_send(ffi_ctx(result), &mi, &script_source);
}

/* Trigger one dash. */
CLAP_SCRIPT_FN(ffi_input_dash, "fn input_dash*()")
void ffi_input_dash(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)params;
    struct message_input mi;
    memset(&mi, 0, sizeof(mi));
    mi.dash = 1;
    message_input_send(ffi_ctx(result), &mi, &script_source);
}
