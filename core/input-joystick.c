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

static inline unsigned int to_press_hold(int state)
{
    return to_press(state) || to_hold(state);
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

void joystick_buttons_update(int joy, const unsigned char *buttons, int nr_buttons)
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

struct joy_map {
    unsigned int (*setter)(int state);
    const char  *name;
    int         offset;
};

#define JOY_MAP(_setter, _field) \
    { .setter = _setter, .name = __stringify(_field), .offset = offsetof(struct message_input, _field) }

struct joy_map joy_map[CLAP_JOY_BTN_COUNT] = {
    [CLAP_JOY_BTN_DPAD_LEFT]  = JOY_MAP(to_press_release, left),
    [CLAP_JOY_BTN_DPAD_RIGHT] = JOY_MAP(to_press_release, right),
    [CLAP_JOY_BTN_DPAD_UP]    = JOY_MAP(to_press_release, up),
    [CLAP_JOY_BTN_DPAD_DOWN]  = JOY_MAP(to_press_release, down),
    [CLAP_JOY_BTN_A]          = JOY_MAP(to_press,         pad_a),
    [CLAP_JOY_BTN_B]          = JOY_MAP(to_press,         pad_b),
    [CLAP_JOY_BTN_X]          = JOY_MAP(to_press,         pad_x),
    [CLAP_JOY_BTN_Y]          = JOY_MAP(to_press,         pad_y),
    [CLAP_JOY_BTN_LB]         = JOY_MAP(to_press_hold,    pad_lb),
    [CLAP_JOY_BTN_RB]         = JOY_MAP(to_press,         pad_rb),
    [CLAP_JOY_BTN_LT]         = JOY_MAP(to_press_hold,    pad_lt),
    [CLAP_JOY_BTN_RT]         = JOY_MAP(to_press_hold,    pad_rt),
    [CLAP_JOY_BTN_BACK]       = JOY_MAP(to_press_hold,    pad_min),
    [CLAP_JOY_BTN_START]      = JOY_MAP(to_press_hold,    pad_plus),
    [CLAP_JOY_BTN_GUIDE]      = JOY_MAP(to_press_hold,    pad_home),
    [CLAP_JOY_BTN_LTHUMB]     = JOY_MAP(to_press_hold,    stick_l),
    [CLAP_JOY_BTN_RTHUMB]     = JOY_MAP(to_press,         stick_r),
    [CLAP_JOY_BTN_LBACK]      = JOY_MAP(to_press,         pad_lback),
    [CLAP_JOY_BTN_RBACK]      = JOY_MAP(to_press,         pad_rback),
};

#ifndef CONFIG_FINAL
void controllers_debug(void)
{
    debug_module *dbgm = ui_igBegin(DEBUG_CONTROLLERS, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        for (unsigned int i = 0; i < NR_JOYS; i++) {
            if (!joystick_present(i))    continue;

            igPushID_Int(i);

            struct joystick *j = &joys[i];

            char label[BUFSIZ];
            snprintf(label, BUFSIZ, "[%d] %s", i, j->name);

            if (igTreeNode_Str(label)) {
                igTextUnformatted("axes", NULL);
                for (unsigned int axis = 0; axis < j->nr_axes; axis++)
                    igText("%d:\t%lf", axis, j->axes[axis]);

                igTextUnformatted("analog buttons", NULL);
                for (unsigned int abtn = 0; abtn < j->nr_axes; abtn++)
                    igText("%d:\t%lf", abtn, j->abuttons[abtn]);

                igTextUnformatted("buttons", NULL);
                for (unsigned int btn = 0; btn < j->nr_buttons; btn++)
                    if (btn < array_size(joy_map) && joy_map[btn].name)
                        igText("%s:\t%d", joy_map[btn].name, (int)j->buttons[btn]);

                igTreePop();
            }

            igPopID();
        }
    }

    ui_igEnd(DEBUG_CONTROLLERS);
}
#endif /* CONFIG_FINAL */

void joysticks_poll(struct clap_context *ctx)
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
                case CLAP_JOY_AXIS_LX:
                    mi.delta_lx = j->axes[t] - j->axes_init[t];
                    break;
                case CLAP_JOY_AXIS_LY:
                    mi.delta_ly = j->axes[t] - j->axes_init[t];
                    break;
                case CLAP_JOY_AXIS_RX:
                    mi.delta_rx = j->axes[t] - j->axes_init[t];
                    break;
                case CLAP_JOY_AXIS_RY:
                    mi.delta_ry = j->axes[t] - j->axes_init[t];
                    break;
                case CLAP_JOY_AXIS_LT:
                    mi.trigger_l = j->axes[t] - j->axes_init[t];
                    break;
                case CLAP_JOY_AXIS_RT:
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

            if (mi.pad_lt && j->abuttons[CLAP_JOY_BTN_LT])
                mi.trigger_l = j->abuttons[CLAP_JOY_BTN_LT];
            if (mi.pad_rt && j->abuttons[CLAP_JOY_BTN_RT])
                mi.trigger_r = j->abuttons[CLAP_JOY_BTN_RT];

            if (mi.pad_plus && to_press(state))
                mi.menu_toggle = 1;
            if (mi.pad_min && to_press(state))
                mi.inv_toggle = 1;
            if (mi.pad_b && to_press(state))
                mi.enter = 1;
            if (mi.pad_a && to_press(state))
                mi.back = 1;
            if (state != JB_NONE)
                count++;
        }

        if (count) {
            /* TODO: display this in input debug UI if necessary */
            message_input_send(ctx, &mi, &j->msg_src);
        }
    }
}
