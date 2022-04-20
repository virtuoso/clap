// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "common.h"
#include "logger.h"
#include "messagebus.h"
#include "input.h"
#include "input-joystick.h"
#include "ui-debug.h"

#define JOY_NAME_MAX 64
#define JOY_THINGS_MAX 64

struct joystick {
    char                name[JOY_NAME_MAX];
    unsigned char       buttons[JOY_THINGS_MAX];
    /* not actually using glfw's hats at the moment */
    // unsigned char       hats[JOY_THINGS_MAX];
    unsigned long       button_state;
    int                 id;
    int                 nr_axes, nr_buttons, nr_hats;
    double              abuttons[JOY_THINGS_MAX];
    double              axes[JOY_THINGS_MAX];
    double              axes_init[JOY_THINGS_MAX];
    struct message_source msg_src;
};

static struct joystick joys[NR_JOYS];

enum {
    JB_PRESS = 0,
    JB_RELEASE,
    JB_HOLD,
    JB_NONE
};

static inline unsigned int to_press(int state)
{
    return state == JB_PRESS;
}

static inline unsigned int to_hold(int state)
{
    return state == JB_HOLD;
}

static inline unsigned int to_release(int state)
{
    return state == JB_RELEASE;
}

static inline unsigned int to_press_hold(int state)
{
    return to_press(state) || to_hold(state);
}

static inline bool jb_press_release(int state)
{
    return to_press(state) || to_release(state);
}

static inline unsigned int to_press_release(int state)
{
    if (state == JB_RELEASE)
        return 2;
    if (state == JB_PRESS)
        return  1;
    return 0;
}

static inline bool joystick_present(int joy)
{
    if (joy >= NR_JOYS)
        return false;

    return !!strlen(joys[joy].name);
}

void joystick_axes_update(int joy, const double *axes, int nr_axes)
{
    if (!joystick_present(joy))
        return;
    
    if (nr_axes > JOY_THINGS_MAX)
        nr_axes = JOY_THINGS_MAX;

    /* new joystick -- reset axes */
    if (!joys[joy].nr_axes) {
        memcpy(joys[joy].axes_init, axes, nr_axes * sizeof(*axes));
        //memset(joys[joy].axes_init, 0, nr_axes * sizeof(*axes));
        dbg("### axis[0]: %f\n", axes[0]);
    }

    joys[joy].nr_axes = nr_axes;
    memcpy(joys[joy].axes, axes, nr_axes * sizeof(*axes));
}

/* XXX-ish */
void joystick_abuttons_update(int joy, const double *abuttons, int nr_buttons)
{
    if (!joystick_present(joy))
        return;
    
    if (nr_buttons > JOY_THINGS_MAX)
        nr_buttons = JOY_THINGS_MAX;

    memcpy(joys[joy].abuttons, abuttons, nr_buttons * sizeof(*abuttons));
}

void joystick_faxes_update(int joy, const float *axes, int nr_axes)
{
    int i;

    if (!joystick_present(joy))
        return;
    
    if (nr_axes > JOY_THINGS_MAX)
        nr_axes = JOY_THINGS_MAX;

    for (i = 0; i < nr_axes; i++) {
        joys[joy].axes[i] = axes[i];
        // dbg("### axis[%d]: %f\n", i, joys[joy].axes[i]);
    }
    
    if (!joys[joy].nr_axes) {
        memcpy(joys[joy].axes_init, joys[joy].axes, nr_axes * sizeof(*axes));
        joys[joy].nr_axes = nr_axes;
    }
}

void joystick_buttons_update(int joy, const char *buttons, int nr_buttons)
{
    if (!joystick_present(joy))
        return;
    
    if (nr_buttons > JOY_THINGS_MAX)
        nr_buttons = JOY_THINGS_MAX;
    
    joys[joy].nr_buttons = nr_buttons;
    memcpy(joys[joy].buttons, buttons, nr_buttons);
}

/* empty string or NULL disables the joystick */
void joystick_name_update(int joy, const char *name)
{
    if (!name)
        name = "";

    /* same name, assuming same joystick */
    if (!strncmp(joys[joy].name, name, JOY_NAME_MAX))
        return;

    strncpy(joys[joy].name, name, JOY_NAME_MAX);
    joys[joy].nr_axes = joys[joy].nr_buttons = 0;
    joys[joy].msg_src.type = -1;
    joys[joy].msg_src.desc = joys[joy].name;
    joys[joy].msg_src.name = joys[joy].name;
}

/* XXX big fat XXX */
#ifdef CONFIG_BROWSER
#define AXIS_LX 0
#define AXIS_LY 1
#define AXIS_RX 2
#define AXIS_RY 3
#define AXIS_LT 4
#define AXIS_RT 5
#define BTN_LEFT  14
#define BTN_RIGHT 15
#define BTN_DOWN  13
#define BTN_UP    12
#define BTN_PADB  0
#define BTN_PADA  1
#define BTN_PADX  3
#define BTN_PADY  2
#define BTN_PADLB 4
#define BTN_PADRB 5
#define BTN_PADLT 6
#define BTN_PADRT 7
#define BTN_MINUS 8
#define BTN_PLUS  9
#define BTN_HOME  16
#define BTN_STICKL 10
#define BTN_STICKR 11
#else
#include <GLFW/glfw3.h>
#define AXIS_LX GLFW_GAMEPAD_AXIS_LEFT_X
#define AXIS_LY GLFW_GAMEPAD_AXIS_LEFT_Y
#define AXIS_RX GLFW_GAMEPAD_AXIS_RIGHT_X
#define AXIS_RY GLFW_GAMEPAD_AXIS_RIGHT_Y
#define AXIS_LT GLFW_GAMEPAD_AXIS_LEFT_TRIGGER
#define AXIS_RT GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER
#define BTN_LEFT  GLFW_GAMEPAD_BUTTON_DPAD_LEFT
#define BTN_RIGHT GLFW_GAMEPAD_BUTTON_DPAD_RIGHT
#define BTN_DOWN  GLFW_GAMEPAD_BUTTON_DPAD_DOWN
#define BTN_UP    GLFW_GAMEPAD_BUTTON_DPAD_UP
#define BTN_PADB  GLFW_GAMEPAD_BUTTON_A
#define BTN_PADA  GLFW_GAMEPAD_BUTTON_B
#define BTN_PADX  GLFW_GAMEPAD_BUTTON_Y
#define BTN_PADY  GLFW_GAMEPAD_BUTTON_X
#define BTN_PADLB GLFW_GAMEPAD_BUTTON_LEFT_BUMPER
#define BTN_PADRB GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER
#define BTN_PADLT 15
#define BTN_PADRT 16
#define BTN_MINUS GLFW_GAMEPAD_BUTTON_BACK
#define BTN_PLUS  GLFW_GAMEPAD_BUTTON_START
#define BTN_HOME  GLFW_GAMEPAD_BUTTON_GUIDE
#define BTN_STICKL GLFW_GAMEPAD_BUTTON_LEFT_THUMB
#define BTN_STICKR GLFW_GAMEPAD_BUTTON_RIGHT_THUMB
#endif

struct joy_map {
    int         offset;
    unsigned int (*setter)(int state);
};

#define JOY_MAP(_setter, _field) \
    { .setter = _setter, .offset = offsetof(struct message_input, _field) }

struct joy_map joy_map[] = {
    [BTN_LEFT]  = JOY_MAP(to_press_release, left),
    [BTN_RIGHT] = JOY_MAP(to_press_release, right),
    [BTN_UP]    = JOY_MAP(to_press_release, up),
    [BTN_DOWN]  = JOY_MAP(to_press_release, down),
    [BTN_PADB]  = JOY_MAP(to_press,         pad_b),
    [BTN_PADA]  = JOY_MAP(to_press,         pad_a),
    [BTN_PADX]  = JOY_MAP(to_press,         pad_x),
    [BTN_PADY]  = JOY_MAP(to_press,         pad_y),
    [BTN_PADLB] = JOY_MAP(to_press,         pad_lb),
    [BTN_PADRB] = JOY_MAP(to_press,         pad_rb),
    [BTN_PADLT] = JOY_MAP(to_press_hold,    pad_lt),
    [BTN_PADRT] = JOY_MAP(to_press_hold,    pad_rt),
    [BTN_MINUS] = JOY_MAP(to_press_hold,    pad_min),
    [BTN_PLUS]  = JOY_MAP(to_press_hold,    pad_plus),
    [BTN_HOME]  = JOY_MAP(to_press_hold,    pad_home),
    [BTN_STICKL] = JOY_MAP(to_press_hold,   stick_l),
    [BTN_STICKR] = JOY_MAP(to_press_hold,   stick_r),
};

void joysticks_poll(void)
{
    struct message_input mi;
    int i, t;

    for (i = 0; i < NR_JOYS; i++) {
        struct joystick *j = &joys[i];
        int count = 0;

        if (!joystick_present(i))
            continue;

        memset(&mi, 0, sizeof(mi));

        /* XXX: below is a mapping of DualShock */
        for (t = 0; t < j->nr_axes; t++)
            if (j->axes[t] != j->axes_init[t]) {
                trace("joystick%d axis%d: %f\n", i, t, j->axes[t]);
                if (fabs(j->axes[t] - j->axes_init[t]) < 0.2)
                    continue;

                switch (t) {
                case AXIS_LX:
                    mi.delta_lx = j->axes[t] - j->axes_init[t];
                    break;
                case AXIS_LY:
                    mi.delta_ly = j->axes[t] - j->axes_init[t];
                    break;
                case AXIS_RX:
                    mi.delta_rx = j->axes[t] - j->axes_init[t];
                    break;
                case AXIS_RY:
                    mi.delta_ry = j->axes[t] - j->axes_init[t];
                    break;
                case AXIS_LT:
                    mi.trigger_l = j->axes[t] - j->axes_init[t];
                    break;
                case AXIS_RT:
                    mi.trigger_r = j->axes[t] - j->axes_init[t];
                    break;
                }
                count++;
            }

        for (t = 0; t < j->nr_buttons; t++) {
            bool pressed = j->button_state & (1ul << t);
            int state = JB_NONE;

            if (j->buttons[t]) {
                state = pressed ? JB_HOLD : JB_PRESS;
                j->button_state |= 1ul << t;
                trace("joystick%d button%d: %d\n", i, t, j->buttons[t]);
            } else {
                j->button_state &= ~(1ul << t);
                if (pressed)
                    state = JB_RELEASE;
            }

            /*
             * TODO: so the idea is to communicate press/hold/release
             * directly to the subscribers, instead of decoding it here;
             * also, the subscribers are already pretty much dealing with
             * xbox-style mapping; the user-level input mapping should
             * only apply to some subscribers (like, "player") and not
             * the others (like, "ui"). Here's another todo.
             */
            if (t < array_size(joy_map) && joy_map[t].setter && state != JB_NONE) {
                unsigned char *val = (void *)&mi + joy_map[t].offset;
                *val = joy_map[t].setter(state);
            }

            if (mi.pad_lt && j->abuttons[BTN_PADLT])
                mi.trigger_l = j->abuttons[BTN_PADLT];
            if (mi.pad_rt && j->abuttons[BTN_PADRT])
                mi.trigger_r = j->abuttons[BTN_PADRT];

            if (mi.pad_plus && to_press(state))
                mi.menu_toggle = 1;
            if (mi.pad_min && to_press(state))
                mi.inv_toggle = 1;
            if (mi.pad_a && to_press(state))
                mi.enter = 1;
            if (mi.pad_b && to_press(state))
                mi.back = 1;
            if (state != JB_NONE)
                count++;
        }

        if (count) {
            // ui_debug_printf("lx: %f ly: %f rx: %f ry: %f lt: %f rt: %f\nbuttons: %016lx",
            //                 mi.delta_lx, mi.delta_ly, mi.delta_rx, mi.delta_ry,
            //                 mi.trigger_l, mi.trigger_r,
            //                 j->button_state);
            message_input_send(&mi, &j->msg_src);
        }
    }
}
