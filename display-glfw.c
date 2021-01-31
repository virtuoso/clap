#define _GNU_SOURCE
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
//#include <GL/glew.h>
#include "display.h"
#include <GLFW/glfw3.h>
#include "common.h"
#include "input.h"

static GLFWwindow *window;
static int width, height;
static display_update update_fn;
static display_resize resize_fn;
static void *update_fn_data;

void gl_title(const char *fmt, ...)
{
    va_list va;
    LOCAL(char, title);

    va_start(va, fmt);
    CHECK(vasprintf(&title, fmt, va));
    va_end(va);
    glfwSetWindowTitle(window, title);
}

static void error_cb(int error, const char *desc)
{
    err("glfw error %d: '%s'\n", error, desc);
}

static void resize_cb(GLFWwindow *window, int w, int h)
{
    width = w;
    height = h;
    resize_fn(w, h);
}

void gl_get_sizes(int *widthp, int *heightp)
{
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    if (widthp)
        *widthp = width;
    if (heightp)
        *heightp = height;
    resize_cb(window, width, height);
}

void gl_resize(int w, int h)
{
    resize_cb(window, w, h);
}

static GLFWmonitor *primary_monitor;
static const GLFWvidmode *primary_monitor_mode;
static int                saved_width, saved_height;

void gl_enter_fullscreen(void)
{
    glfwSetWindowMonitor(window, primary_monitor, 0, 0, 
                         primary_monitor_mode->width,
                         primary_monitor_mode->height,
                         primary_monitor_mode->refreshRate
                         );
    saved_width = width;
    saved_height = height;
    gl_resize(primary_monitor_mode->width, primary_monitor_mode->height);
}

void gl_leave_fullscreen(void)
{
    glfwSetWindowMonitor(window, NULL, 0, 0, saved_width, saved_height, 0);
    gl_resize(saved_width, saved_height);
}

void gl_init(const char *title, int w, int h, display_update update, void *update_data, display_resize resize)
{
    width = w;
    height = h;
    update_fn = update;
    update_fn_data = update_data;
    resize_fn = resize;

    if (!glfwInit()) {
        err("failed to initialize GLFW\n");
        return;
    }
    primary_monitor = glfwGetPrimaryMonitor();
    primary_monitor_mode = glfwGetVideoMode(primary_monitor);

    glfwSetErrorCallback(error_cb);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) {
        err("failed to create GLFW window\n");
        return;
    }

    glfwSetFramebufferSizeCallback(window, resize_cb);
    glfwMakeContextCurrent(window);
    if (glewInit() != GLEW_OK) {
        err("failed to initialize GLEW\n");
        return;
    }
    msg("GL initialized\n");
}

void gl_request_exit(void)
{
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void gl_main_loop(void)
{
    while (!glfwWindowShouldClose(window)) {
        update_fn(update_fn_data);
    }
}

void gl_done(void)
{
    glfwDestroyWindow(window);
    glfwTerminate();
}

static struct message_source keyboard_source = {
    .name   = "keyboard",
    .desc   = "keyboard and mouse",
    .type   = MST_KEYBOARD,
};

static void key_cb(struct GLFWwindow *window, int key, int scancode, int action, int mods)
{
    struct message_input mi;

    if (action == GLFW_RELEASE)
        return;

    memset(&mi, 0, sizeof(mi));
    trace("key %d scancode %d action %d mods %d\n",
         key, scancode, action, mods);
    switch (key) {
    case GLFW_KEY_RIGHT: /* ArrowRight */
        if (mods & GLFW_MOD_SHIFT)
            mi.yaw_right = 1;
        else
            mi.right = 1;
        break;
    case GLFW_KEY_LEFT: /* ArrowLeft */
        if (mods & GLFW_MOD_SHIFT)
            mi.yaw_left = 1;
        else
            mi.left = 1;
        break;
    case GLFW_KEY_DOWN: /* ArrowDown */
        if (mods & GLFW_MOD_SHIFT)
            mi.pitch_down = 1;
        else
            mi.down = 1;
        break;
    case GLFW_KEY_UP: /* ArrowUp */
        if (mods & GLFW_MOD_SHIFT)
            mi.pitch_up = 1;
        else
            mi.up = 1;
        break;
    case GLFW_KEY_SPACE:
        if (mods & GLFW_MOD_SHIFT)
            mi.focus_prev = 1;
        else if (mods & GLFW_MOD_CONTROL)
            mi.focus_cancel = 1;
        else
            mi.focus_next = 1;
        break;
    case GLFW_KEY_M:
        mi.menu_toggle = 1;
        break;
    case GLFW_KEY_F1:
        mi.fullscreen = 1;
        break;
    case GLFW_KEY_F10:
        mi.autopilot = 1;
        break;
    case GLFW_KEY_F12:
        mi.verboser = 1;
        break;
    case GLFW_KEY_ESCAPE:
        mi.exit = 1;
        break;
    };
    message_input_send(&mi, &keyboard_source);
}

static void pointer_cb(struct GLFWwindow *window, double x, double y)
{
    trace("pointer at %g,%g\n", x, y);
}

static void scroll_cb(struct GLFWwindow *window, double xoff, double yoff)
{
    struct message_input mi;

    trace("scrolling %g,%g\n", xoff, yoff);

    memset(&mi, 0, sizeof(mi));
    mi.delta_lx = xoff;
    mi.delta_ly = yoff;
    message_input_send(&mi, &keyboard_source);
}

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

struct joystick {
    int id;
    const char *name;
    const unsigned char *buttons;
    const unsigned char *hats;
    unsigned long button_state;
    int nr_axes, nr_buttons, nr_hats;
    const float *axes, *axes_init;
    struct message_source msg_src;
};

static struct joystick *joys[16];
static void joystick_init(void)
{
    int i, joy, count = 0;
    struct joystick *j;

    for (i = GLFW_JOYSTICK_1; i < GLFW_JOYSTICK_16; i++) {
        joy = glfwJoystickPresent(i);

        if (!joy)
            continue;

        j = malloc(sizeof(*j));
        if (!j)
            return;

        j->id = joy;
        j->axes = glfwGetJoystickAxes(i, &j->nr_axes);
        j->axes_init = memdup(j->axes, j->nr_axes * sizeof(*j->axes));
        j->buttons = glfwGetJoystickButtons(i, &j->nr_buttons);
        j->hats = glfwGetJoystickHats(i, &j->nr_hats);
        j->name = glfwGetJoystickName(i);
        j->msg_src.type = -1;
        CHECK(asprintf(&j->msg_src.name, "joystick%d", joy));
        j->msg_src.desc = j->name;
        msg("joystick '%s' (%d) found: axes: %d buttons: %d hats: %d\n",
            j->name, joy, j->nr_axes, j->nr_buttons, j->nr_hats);
        joys[i] = j;
        count++;
    }

    msg("fount %d joysticks\n", count);
}

static void joysticks_poll(void)
{
    struct message_input mi;
    int i, t;

    for (i = GLFW_JOYSTICK_1; i < GLFW_JOYSTICK_16; i++) {
        struct joystick *j = joys[i];
        int count = 0;

        if (!j)
            continue;

        j->axes = glfwGetJoystickAxes(i, &j->nr_axes);
        j->buttons = glfwGetJoystickButtons(i, &j->nr_buttons);
        j->hats = glfwGetJoystickHats(i, &j->nr_hats);

        memset(&mi, 0, sizeof(mi));
        for (t = 0; t < j->nr_axes; t++)
            if (j->axes[t] != j->axes_init[t]) {
                trace("joystick%d axis%d: %f\n", i, t, j->axes[t]);
                /* axis have better resolution, but hats are faster */
                if (j->axes[t] > j->axes_init[t]) {
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
                }
                switch (t) {
                case 0:
                    mi.delta_lx = j->axes[t] - j->axes_init[t];
                    break;
                case 1:
                    mi.delta_ly = j->axes[t] - j->axes_init[t];
                    break;
                case 3:
                    mi.delta_rx = j->axes[t] - j->axes_init[t];
                    if (j->axes[t] > j->axes_init[t])
                        mi.yaw_right = 1;
                    else if (j->axes[t] < j->axes_init[t])
                        mi.yaw_left = 1;
                    break;
                case 4:
                    mi.delta_ry = j->axes[t] - j->axes_init[t];

                    break;
                case 2:
                    mi.trigger_l = j->axes[t] - j->axes_init[t];
                    break;
                case 5:
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

            if (t == 16 && jb_press(state))
                mi.left = 1;
            else if (t == 14 && jb_press(state))
                mi.right = 1;
            else if (t == 15 && jb_press(state))
                mi.down = 1;
            else if (t == 13 && jb_press(state))
                mi.up = 1;
            else if (t == 0 && jb_press_hold(state))
                mi.pad_b = 1;
            else if (t == 1 && jb_press_hold(state))
                mi.pad_a = 1;
            else if (t == 2 && jb_press_hold(state))
                mi.pad_x = 1;
            else if (t == 3 && jb_press_hold(state))
                mi.pad_y = 1;
            else if (t == 4 && jb_press_hold(state))
                mi.pad_lb = 1;
            else if (t == 5 && jb_press_hold(state))
                mi.pad_rb = 1;
            else if (t == 6 && jb_press_hold(state))
                mi.pad_lt = 1;
            else if (t == 7 && jb_press_hold(state))
                mi.pad_rt = 1;
            else if (t == 8 && jb_press_hold(state))
                mi.pad_min = 1;
            else if (t == 9 && jb_press_hold(state))
                mi.pad_plus = 1;
            else if (t == 10 && jb_press_hold(state))
                mi.pad_home = 1;
            else if (t == 11 && jb_press_hold(state))
                mi.stick_l = 1;
            else if (t == 12 && jb_press_hold(state))
                mi.stick_r = 1;

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

int platform_input_init(void)
{
    glfwSetKeyCallback(window, key_cb);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetCursorPosCallback(window, pointer_cb);
    glfwSetScrollCallback(window, scroll_cb);

    joystick_init();
    msg("input initialized\n");

    return 0;
}

void gl_swap_buffers(void)
{
    glfwSwapBuffers(window);
    glfwPollEvents();
    joysticks_poll();
}
