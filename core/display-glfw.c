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
#include "input-joystick.h"

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
    const unsigned char *exts;

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

    exts = glGetString(GL_EXTENSIONS);

    msg("GL initialized extensions: %s\n", exts);
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
    case GLFW_KEY_TAB:
        mi.tab = 1;
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

static void glfw_joysticks_poll(void)
{
    struct message_input mi;
    int i;

    for (i = GLFW_JOYSTICK_1; i < GLFW_JOYSTICK_16; i++) {
        const char *name = glfwGetJoystickName(i);
        int nr_axes, nr_buttons;
        const char *buttons;
        const float *axes;

        joystick_name_update(i - GLFW_JOYSTICK_1, name);

        if (!name)
            continue;

        axes = glfwGetJoystickAxes(i, &nr_axes);
        joystick_faxes_update(i - GLFW_JOYSTICK_1, axes, nr_axes);

        buttons = glfwGetJoystickButtons(i, &nr_buttons);
        joystick_buttons_update(i - GLFW_JOYSTICK_1, buttons, nr_buttons);
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

    msg("input initialized\n");

    return 0;
}

void gl_swap_buffers(void)
{
    glfwSwapBuffers(window);
    glfwPollEvents();
    /* XXX: move to the start of frame code? */
    glfw_joysticks_poll();
    joysticks_poll();
}
