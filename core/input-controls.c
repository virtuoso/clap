// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "clap.h"
#include "common.h"
#include "input-controls.h"
#include "input-joystick.h"
#include "settings.h"

#define IC_GROUP                "input_controls"
#define IC_KEY_GAMEPAD_POLICY   "gamepad_policy"
#define IC_KEY_GAMEPAD_NAME     "gamepad_name"
#define IC_KEY_USE_MOUSE        "use_mouse_for_camera"
#define IC_KEY_SENSITIVITY      "mouse_sensitivity"
#define IC_KEY_INVERT_Y         "invert_y"

#define IC_GAMEPAD_NAME_MAX     64

struct ic_state {
    input_controls_gamepad  gamepad_policy;
    char                    gamepad_name[IC_GAMEPAD_NAME_MAX];
    bool                    use_mouse;
    float                   mouse_sensitivity;
    bool                    invert_y;
    bool                    ready;
};

static struct ic_state _ic;

static JsonNode *ic_group(struct settings *rs)
{
    return settings_find_get(rs, NULL, IC_GROUP, JSON_OBJECT);
}

cerr input_controls_init(struct settings *rs)
{
    if (!rs)
        return CERR_INVALID_OPERATION;

    JsonNode *grp = ic_group(rs);
    if (!grp)
        return CERR_INVALID_OPERATION;

    const char *policy = settings_get_str(rs, grp, IC_KEY_GAMEPAD_POLICY);
    if (policy && !strcmp(policy, "any"))
        _ic.gamepad_policy = INPUT_GAMEPAD_ANY;
    else if (policy && !strcmp(policy, "named"))
        _ic.gamepad_policy = INPUT_GAMEPAD_NAMED;
    else
        _ic.gamepad_policy = INPUT_GAMEPAD_NONE;

    const char *name = settings_get_str(rs, grp, IC_KEY_GAMEPAD_NAME);
    if (name)
        strncpy(_ic.gamepad_name, name, sizeof(_ic.gamepad_name) - 1);
    else
        _ic.gamepad_name[0] = '\0';

    _ic.use_mouse = settings_get_bool(rs, grp, IC_KEY_USE_MOUSE);

    JsonNode *sens = settings_get(rs, grp, IC_KEY_SENSITIVITY);
    _ic.mouse_sensitivity =
        (sens && sens->tag == JSON_NUMBER) ? (float)sens->number_ : 1.0f;
    if (_ic.mouse_sensitivity < 0.1f)
        _ic.mouse_sensitivity = 0.1f;

    _ic.invert_y = settings_get_bool(rs, grp, IC_KEY_INVERT_Y);
    _ic.ready = true;

    return CERR_OK;
}

void input_controls_done(void)
{
    _ic.ready = false;
}

input_controls_gamepad input_controls_gamepad_policy(void)
{
    return _ic.gamepad_policy;
}

const char *input_controls_saved_gamepad_name(void)
{
    return _ic.gamepad_name[0] ? _ic.gamepad_name : NULL;
}

const char *input_controls_active_gamepad_name(void)
{
    if (!_ic.ready)
        return NULL;

    switch (_ic.gamepad_policy) {
    case INPUT_GAMEPAD_NONE:
        return NULL;
    case INPUT_GAMEPAD_ANY:
        for (int i = 0; i < NR_JOYS; i++) {
            const char *n = joystick_name_at(i);
            if (n)
                return n;
        }
        return NULL;
    case INPUT_GAMEPAD_NAMED:
        if (_ic.gamepad_name[0]) {
            for (int i = 0; i < NR_JOYS; i++) {
                const char *n = joystick_name_at(i);
                if (n && !strcmp(n, _ic.gamepad_name))
                    return n;
            }
        }
        for (int i = 0; i < NR_JOYS; i++) {
            const char *n = joystick_name_at(i);
            if (n)
                return n;
        }
        return NULL;
    }
    return NULL;
}

bool   input_controls_use_mouse(void)           { return _ic.use_mouse; }
float  input_controls_mouse_sensitivity(void)   { return _ic.mouse_sensitivity; }
bool   input_controls_invert_y(void)            { return _ic.invert_y; }

static const char *policy_str(input_controls_gamepad p)
{
    switch (p) {
    case INPUT_GAMEPAD_ANY:     return "any";
    case INPUT_GAMEPAD_NAMED:   return "named";
    case INPUT_GAMEPAD_NONE:
    default:                    return "none";
    }
}

void input_controls_set_gamepad(struct clap_context *ctx,
                                input_controls_gamepad policy, const char *name)
{
    struct settings *rs = clap_get_settings(ctx);
    JsonNode *grp = rs ? ic_group(rs) : NULL;

    _ic.gamepad_policy = policy;
    if (policy == INPUT_GAMEPAD_NAMED && name) {
        strncpy(_ic.gamepad_name, name, sizeof(_ic.gamepad_name) - 1);
        _ic.gamepad_name[sizeof(_ic.gamepad_name) - 1] = '\0';
    } else {
        _ic.gamepad_name[0] = '\0';
    }

    if (grp) {
        settings_set_string(rs, grp, IC_KEY_GAMEPAD_POLICY, policy_str(policy));
        settings_set_string(rs, grp, IC_KEY_GAMEPAD_NAME, _ic.gamepad_name);
    }
}

void input_controls_set_use_mouse(struct clap_context *ctx, bool on)
{
    struct settings *rs = clap_get_settings(ctx);
    JsonNode *grp = rs ? ic_group(rs) : NULL;

    _ic.use_mouse = on;
    if (grp)
        settings_set_bool(rs, grp, IC_KEY_USE_MOUSE, on);
    clap_update_mouse_capture(ctx);
}

void input_controls_set_mouse_sensitivity(struct clap_context *ctx, float sens)
{
    struct settings *rs = clap_get_settings(ctx);
    JsonNode *grp = rs ? ic_group(rs) : NULL;

    if (sens < 0.1f) sens = 0.1f;
    if (sens > 5.0f) sens = 5.0f;
    _ic.mouse_sensitivity = sens;
    if (grp)
        settings_set_num(rs, grp, IC_KEY_SENSITIVITY, sens);
}

void input_controls_set_invert_y(struct clap_context *ctx, bool on)
{
    struct settings *rs = clap_get_settings(ctx);
    JsonNode *grp = rs ? ic_group(rs) : NULL;

    _ic.invert_y = on;
    if (grp)
        settings_set_bool(rs, grp, IC_KEY_INVERT_Y, on);
}
