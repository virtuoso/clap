#include "common.h"
#include "logger.h"
#include "messagebus.h"
#include "input.h"
#include "input-joystick.h"

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

static inline bool jb_press(int state)
{
    return state == JB_PRESS;
}

static inline bool jb_hold(int state)
{
    return state == JB_HOLD;
}

static inline bool jb_press_hold(int state)
{
    return jb_press(state) || jb_hold(state);
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

    for (i = 0; i < nr_axes; i++)
        joys[joy].axes[i] = axes[i];
    
    if (!joys[joy].nr_axes)
        memcpy(joys[joy].axes_init, joys[joy].axes, nr_axes * sizeof(*axes));
    joys[joy].nr_axes = nr_axes;
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

struct joy_map {

};

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
#define AXIS_LX 0
#define AXIS_LY 1
#define AXIS_RX 3
#define AXIS_RY 4
#define AXIS_LT 2
#define AXIS_RT 5
#define BTN_LEFT  16
#define BTN_RIGHT 14
#define BTN_DOWN  15
#define BTN_UP    13
#define BTN_PADB  0
#define BTN_PADA  1
#define BTN_PADX  2
#define BTN_PADY  3
#define BTN_PADLB 4
#define BTN_PADRB 5
#define BTN_PADLT 6
#define BTN_PADRT 7
#define BTN_MINUS 8
#define BTN_PLUS  9
#define BTN_HOME  10
#define BTN_STICKL 11
#define BTN_STICKR 12
#endif

void ui_debug_printf(const char *fmt, ...);

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
                /* axis have better resolution, but hats are faster */
                /*if (j->axes[t] > j->axes_init[t]) {
                    switch (t) {
                    case 1:
                        mi.down = 1;
                        break;
                    case 0:
                        mi.right = 1;
                        break;
                    default:
                        break;
                    }
                }
                if (j->axes[t] < j->axes_init[t]) {
                    switch (t) {
                    case 1:
                        mi.up = 1;
                        break;
                    case 0:
                        mi.left = 1;
                        break;
                    default:
                        break;
                    }
                }*/
                switch (t) {
                case AXIS_LX:
                    mi.delta_lx = j->axes[t] - j->axes_init[t];
                    break;
                case AXIS_LY:
                    mi.delta_ly = j->axes[t] - j->axes_init[t];
                    break;
                case AXIS_RX:
                    mi.delta_rx = j->axes[t] - j->axes_init[t];
                    if (j->axes[t] > j->axes_init[t])
                        mi.yaw_right = 1;
                    else if (j->axes[t] < j->axes_init[t])
                        mi.yaw_left = 1;
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

            if (t == BTN_LEFT && jb_press(state))
                mi.left = 1;
            else if (t == BTN_RIGHT && jb_press(state))
                mi.right = 1;
            else if (t == BTN_DOWN && jb_press(state))
                mi.down = 1;
            else if (t == BTN_UP && jb_press(state))
                mi.up = 1;
            else if (t == BTN_PADB && jb_press_hold(state))
                mi.pad_b = 1;
            else if (t == BTN_PADA && jb_press_hold(state))
                mi.pad_a = 1;
            else if (t == BTN_PADX && jb_press_hold(state))
                mi.pad_x = 1;
            else if (t == BTN_PADY && jb_press_hold(state))
                mi.pad_y = 1;
            else if (t == BTN_PADLB && jb_press_hold(state))
                mi.pad_lb = 1;
            else if (t == BTN_PADRB && jb_press_hold(state))
                mi.pad_rb = 1;
            else if (t == BTN_PADLT && jb_press_hold(state))
                mi.pad_lt = 1;
            else if (t == BTN_PADRT && jb_press_hold(state))
                mi.pad_rt = 1;
            else if (t == BTN_MINUS && jb_press_hold(state))
                mi.pad_min = 1;
            else if (t == BTN_PLUS && jb_press_hold(state))
                mi.pad_plus = 1;
            else if (t == BTN_HOME && jb_press_hold(state))
                mi.pad_home = 1;
            else if (t == BTN_STICKL && jb_press_hold(state))
                mi.stick_l = 1;
            else if (t == BTN_STICKR && jb_press_hold(state))
                mi.stick_r = 1;

            if (mi.pad_lt && j->abuttons[BTN_PADLT])
                mi.trigger_l = j->abuttons[BTN_PADLT];
            if (mi.pad_rt && j->abuttons[BTN_PADRT])
                mi.trigger_r = j->abuttons[BTN_PADRT];

            if (mi.pad_plus && jb_press(state))
                mi.menu_toggle = 1;
            if (mi.pad_a && jb_press(state))
                mi.enter = 1;
            if (mi.pad_b && jb_press(state))
                mi.back = 1;
            if (state != JB_NONE)
                count++;
        }

        if (count)
            message_input_send(&mi, &j->msg_src);
    }
}
