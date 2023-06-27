// SPDX-License-Identifier: Apache-2.0
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
#include "input-keyboard.h"
#include "render.h"

static GLFWwindow *window;
static int width, height;
static display_update update_fn;
static display_resize resize_fn;
static void *update_fn_data;

bool gl_does_vao(void)
{
#ifdef CONFIG_GLES
    return false;
#else
    return true;
#endif
}

int gl_refresh_rate(void)
{
    GLFWmonitor *monitor = glfwGetWindowMonitor(window);

    if (!monitor)
        monitor = glfwGetPrimaryMonitor();
    if (!monitor)
        return 60;

    return glfwGetVideoMode(monitor)->refreshRate;
}

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
    abort();
}

static void resize_cb(GLFWwindow *window, int w, int h)
{
    width = w;
    height = h;
    resize_fn(update_fn_data, w, h);
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
    const unsigned char *ext, *vendor, *renderer, *glver, *shlangver;
    GLint nr_exts;
    GLenum ret;
    int i;

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
    glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef CONFIG_GLES
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) {
        err("failed to create GLFW window\n");
        return;
    }

    glfwSetFramebufferSizeCallback(window, resize_cb);
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    ret = glewInit();
    if (ret != GLEW_OK) {
        err("failed to initialize GLEW: %s\n", glewGetErrorString(ret));
        return;
    }
    if (glfwExtensionSupported("WGL_EXT_swap_control_tear") ||
        glfwExtensionSupported("GLX_EXT_swap_control_tear"))
        glfwSwapInterval(-1);
    else
        glfwSwapInterval(1);

    vendor    = glGetString(GL_VENDOR);
    renderer  = glGetString(GL_RENDERER);
    glver     = glGetString(GL_VERSION);
    shlangver = glGetString(GL_SHADING_LANGUAGE_VERSION);
    msg("GL vendor '%s' renderer '%s' GL version %s GLSL version %s\n",
        vendor, renderer, glver, shlangver);
    glGetIntegerv(GL_NUM_EXTENSIONS, &nr_exts);
    for (i = 0; i < nr_exts; i++) {
        ext = glGetStringi(GL_EXTENSIONS, i);
        msg("GL extension: '%s'\n", ext);
    }
    // msg("GL initialized extensions: %s\n", exts);
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
    unsigned int press;

    trace("key %d scancode %d action %d mods %d\n",
          key, scancode, action, mods);

    switch (action) {
        case GLFW_REPEAT:
            press = KEY_HOLD;
            break;
        case GLFW_PRESS:
            press = KEY_PRESS;
            break;
        case GLFW_RELEASE:
            press = KEY_RELEASE;
            break;
    }
    memset(&mi, 0, sizeof(mi));
    switch (key) {
    case GLFW_KEY_SPACE:
        if (mods & GLFW_MOD_SHIFT)
            mi.focus_prev = 1;
        else if (mods & GLFW_MOD_CONTROL)
            mi.focus_cancel = 1;
        else if (mods & GLFW_MOD_ALT)
            mi.focus_next = 1;
        else
            mi.space = 1;
        break;
    case GLFW_KEY_ESCAPE:
        mi.menu_toggle = press == 1;
        break;
    default:
        key_event(&keyboard_source, key, NULL, mods, press);
        return;
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
        const unsigned char *buttons;
        const float *axes;

        joystick_name_update(i - GLFW_JOYSTICK_1, name);

        if (!name)
            continue;

        if (glfwJoystickIsGamepad(i)) {
            GLFWgamepadstate state;

            glfwGetGamepadState(i, &state);
            state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = (state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1) / 2;
            state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = (state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1) / 2;
            joystick_faxes_update(i - GLFW_JOYSTICK_1, state.axes, array_size(state.axes));
            joystick_buttons_update(i - GLFW_JOYSTICK_1, state.buttons, array_size(state.buttons));
        } else {
            axes = glfwGetJoystickAxes(i, &nr_axes);
            joystick_faxes_update(i - GLFW_JOYSTICK_1, axes, nr_axes);

            buttons = glfwGetJoystickButtons(i, &nr_buttons);
            joystick_buttons_update(i - GLFW_JOYSTICK_1, buttons, nr_buttons);
        }
    }
}

#include "librarian.h"
int platform_input_init(void)
{
    struct lib_handle *lh;
    char *cdb;
    size_t sz;

    glfwSetKeyCallback(window, key_cb);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetCursorPosCallback(window, pointer_cb);
    glfwSetScrollCallback(window, scroll_cb);

    lh = lib_read_file(RES_ASSET, "gamecontrollerdb.txt", (void **)&cdb, &sz);
    glfwUpdateGamepadMappings(cdb);
    ref_put_last_ref(&lh->ref);

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
