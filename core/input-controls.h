/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_INPUT_CONTROLS_H__
#define __CLAP_INPUT_CONTROLS_H__

#include "error.h"

struct clap_context;
struct settings;

/**
 * enum input_controls_gamepad - active gamepad selection policy
 * @INPUT_GAMEPAD_NONE: no gamepad selected; all gamepad input is dropped
 * @INPUT_GAMEPAD_ANY:  any present gamepad is accepted
 * @INPUT_GAMEPAD_NAMED: a specific gamepad name is preferred; if absent,
 *                       falls back to the first present gamepad
 */
typedef enum input_controls_gamepad {
    INPUT_GAMEPAD_NONE = 0,
    INPUT_GAMEPAD_ANY,
    INPUT_GAMEPAD_NAMED,
} input_controls_gamepad;

/**
 * input_controls_init() - load controls settings
 * @rs:     settings handle; must be ready. Intended to be called from a
 *          clap_settings_cb(), before clap_init() returns and ctx->settings
 *          is assigned.
 *
 * Reads the @input_controls settings group and populates the in-memory
 * cache used by input_controls_*() accessors.
 * Return: %CERR_OK on success.
 */
cerr input_controls_init(struct settings *rs);

/**
 * input_controls_done() - release controls state
 */
void input_controls_done(void);

/**
 * input_controls_gamepad_policy() - read saved gamepad policy
 */
input_controls_gamepad input_controls_gamepad_policy(void);

/**
 * input_controls_saved_gamepad_name() - saved gamepad name (NULL if none)
 */
const char *input_controls_saved_gamepad_name(void);

/**
 * input_controls_active_gamepad_name() - resolve the effective gamepad
 *
 * Applies the precedence from &input_controls_gamepad:
 *  - %INPUT_GAMEPAD_NONE:  NULL
 *  - %INPUT_GAMEPAD_ANY:   first present gamepad name, or NULL
 *  - %INPUT_GAMEPAD_NAMED: saved name if present, else first present
 *
 * Return: gamepad name string or NULL when no gamepad should be consumed.
 */
const char *input_controls_active_gamepad_name(void);

bool   input_controls_use_mouse(void);
float  input_controls_mouse_sensitivity(void);
bool   input_controls_invert_y(void);

void input_controls_set_gamepad(struct clap_context *ctx,
                                input_controls_gamepad policy, const char *name);
void input_controls_set_use_mouse(struct clap_context *ctx, bool on);
void input_controls_set_mouse_sensitivity(struct clap_context *ctx, float sens);
void input_controls_set_invert_y(struct clap_context *ctx, bool on);

#endif /* __CLAP_INPUT_CONTROLS_H__ */
