// SPDX-License-Identifier: Apache-2.0
#include "config.h"
#ifdef CONFIG_RENDERER_OPENGL
#include <GL/glew.h>
#endif /* CONFIG_RENDERER_OPENGL */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "clap.h"
#include "display.h"
#include <GLFW/glfw3.h>
#include "ui-debug.h"
#include "common.h"
#include "input.h"
#include "input-joystick.h"
#include "input-keyboard.h"
#include "render.h"

static GLFWwindow *window;
static int width, height;
static renderer_t *renderer;
static display_update_cb update_fn;
static display_resize_cb resize_fn;
static void *update_fn_data;
static int refresh_rate;

static int __display_refresh_rate(void)
{
    GLFWmonitor *monitor = glfwGetWindowMonitor(window);

    if (!monitor)
        monitor = glfwGetPrimaryMonitor();
    if (!monitor)
        return 60;

    refresh_rate = glfwGetVideoMode(monitor)->refreshRate;
    return refresh_rate;
}

int display_refresh_rate(void)
{
    if (!refresh_rate)
        refresh_rate = __display_refresh_rate();

    return refresh_rate;
}

void display_title(const char *fmt, ...)
{
    va_list va;
    LOCAL(char, title);

    va_start(va, fmt);
    cres(int) res = mem_vasprintf(&title, fmt, va);
    va_end(va);

    if (!IS_CERR(res))
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
    refresh_rate = __display_refresh_rate();
    resize_fn(update_fn_data, w, h);
}

static void move_cb(GLFWwindow *window, int x, int y)
{
    /*
     * force refresh rate update, in case we moved to a different
     * monitor
     */
    refresh_rate = __display_refresh_rate();
    /* store new window position in settings */
    display_get_sizes(NULL, NULL);
}

void display_get_sizes(int *widthp, int *heightp)
{
    glfwGetFramebufferSize(window, &width, &height);

    renderer_viewport(renderer, 0, 0, width, height);

    if (widthp)
        *widthp = width;
    if (heightp)
        *heightp = height;
    resize_cb(window, width, height);
}

void display_resize(int w, int h)
{
    resize_cb(window, w, h);
}

static GLFWmonitor *primary_monitor;
static const GLFWvidmode *primary_monitor_mode;
static int                saved_width, saved_height;

void display_enter_fullscreen(void)
{
    glfwSetWindowMonitor(window, primary_monitor, 0, 0, 
                         primary_monitor_mode->width,
                         primary_monitor_mode->height,
                         primary_monitor_mode->refreshRate
                         );
    saved_width = width;
    saved_height = height;
    display_resize(primary_monitor_mode->width, primary_monitor_mode->height);
}

void display_leave_fullscreen(void)
{
    glfwSetWindowMonitor(window, NULL, 0, 0, saved_width, saved_height, 0);
    display_resize(saved_width, saved_height);
}

void display_set_window_pos_size(int x, int y, int w, int h)
{
    glfwSetWindowPos(window, x, y);
    glfwSetWindowSize(window, w, h);
}

void display_get_window_pos_size(int *x, int *y, int *w, int *h)
{
    glfwGetWindowPos(window, x, y);
    glfwGetWindowSize(window, w, h);
}

#ifdef CONFIG_RENDERER_OPENGL
static cerr display_gl_init(const char *title, int *pmajor, int *pminor, bool *pcore_profile)
{
    const unsigned char *vendor, *renderer, *glver, *shlangver;
    int glew_ret = -1;

#ifdef CONFIG_GLES
    *pcore_profile = false;
    *pmajor = 3;
    *pminor = 1;
#else
    *pcore_profile = true;
    *pmajor = 4;
    *pminor = 1;
#endif /* CONFIG_GLES */

restart:
    glfwWindowHint(GLFW_SAMPLES, 4);
    if (!*pcore_profile)
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, *pmajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, *pminor);
    glfwWindowHint(GLFW_OPENGL_PROFILE,
                   *pcore_profile ? GLFW_OPENGL_CORE_PROFILE : GLFW_OPENGL_ANY_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) {
        err("failed to create GLFW window\n");
        return CERR_INITIALIZATION_FAILED;
    }

    glfwSetWindowPosCallback(window, move_cb);
    glfwSetFramebufferSizeCallback(window, resize_cb);

    glfwMakeContextCurrent(window);

    if (glew_ret) {
        glewExperimental = GL_TRUE;
        glew_ret = glewInit();
        if (glew_ret != GLEW_OK) {
            err("failed to initialize GLEW: %s\n", glewGetErrorString(glew_ret));
            return CERR_NOMEM;
        }
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

    int _major, _minor;
    if (sscanf((const char *)glver, "%d.%d", &_major, &_minor) == 2) {
        const char *profile;
        int restart = 0;

        profile = strstr("Profile", (const char *)glver);
        if (!profile)
            profile = strstr("profile", (const char *)glver);

        if (profile) {
            profile = strstr("Core", (const char *)glver);
            if (!!profile && !*pcore_profile) {
                *pcore_profile = true;
                restart++;
            }
        }

        if (_major > *pmajor || _minor > *pminor) {
            *pmajor = _major;
            *pminor = _minor;
            restart++;
        }

        if (restart) {
            glfwDestroyWindow(window);
            goto restart;
        }
    }

    return CERR_OK;
}
#else
static inline cerr display_gl_init(const char *name, int *pwidth, int *pheight, bool *pcore) { return CERR_NOT_SUPPORTED; }
#endif /* CONFIG_RENDERER_OPENGL */

cerr_check display_init(struct clap_context *ctx, display_update_cb update_cb, display_resize_cb resize_cb)
{
    struct clap_config *cfg = clap_get_config(ctx);
    bool core_profile;
    int major, minor;

    renderer = clap_get_renderer(ctx);
    width = cfg->width;
    height = cfg->height;
    update_fn = update_cb;
    update_fn_data = ctx;
    resize_fn = resize_cb;

    if (!glfwInit()) {
        err("failed to initialize GLFW\n");
        return CERR_INITIALIZATION_FAILED;
    }
    primary_monitor = glfwGetPrimaryMonitor();
    primary_monitor_mode = glfwGetVideoMode(primary_monitor);

    glfwSetErrorCallback(error_cb);

    cerr err = display_gl_init(cfg->title, &major, &minor, &core_profile);
    if (err != CERR_OK)
        return CERR_INITIALIZATION_FAILED;

    dbg("GLFW: %d.%d %s profile\n", major, minor, core_profile ? "Core" : "Any");

    renderer_init(renderer);
    renderer_set_version(renderer, major, minor,
                         core_profile ? RENDERER_CORE_PROFILE : RENDERER_ANY_PROFILE);

    return CERR_OK;
}

void display_debug_ui_init(struct clap_context *ctx)
{
    imgui_init(ctx, window, width, height);
}

void display_request_exit(void)
{
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void display_main_loop(void)
{
    while (!glfwWindowShouldClose(window)) {
        update_fn(update_fn_data);
    }
}

void display_done(void)
{
    imgui_done();
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
        default:
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
        mi.menu_toggle = press == KEY_PRESS;
        break;
    default:
        key_event(&keyboard_source, key, NULL, mods, press);
        return;
    };
    message_input_send(&mi, &keyboard_source);
}

static void pointer_cb(struct GLFWwindow *window, double x, double y)
{
    struct message_input mi;

    if (__ui_mouse_event_propagate())
        return;

    memset(&mi, 0, sizeof(mi));
    mi.mouse_move = 1;
    mi.x = x;
    mi.y = y;
    message_input_send(&mi, &keyboard_source);
}

void click_cb(GLFWwindow *window, int button, int action, int mods)
{
    struct message_input mi;
    double x, y;

    if (__ui_mouse_event_propagate())
        return;

    if (action != GLFW_PRESS)
        return;

    glfwGetCursorPos(window, &x, &y);

    memset(&mi, 0, sizeof(mi));
    mi.mouse_click = 1;
    mi.x = x;
    mi.y = y;
    message_input_send(&mi, &keyboard_source);
}

static void scroll_cb(struct GLFWwindow *window, double xoff, double yoff)
{
    struct message_input mi;

    if (__ui_mouse_event_propagate())
        return;

    trace("scrolling %g,%g\n", xoff, yoff);

    memset(&mi, 0, sizeof(mi));
    mi.delta_lx = xoff;
    mi.delta_ly = yoff;
    message_input_send(&mi, &keyboard_source);
}

static void glfw_joysticks_poll(void)
{
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
    LOCAL(lib_handle, lh);
    char *cdb;
    size_t sz;

    glfwSetKeyCallback(window, key_cb);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetCursorPosCallback(window, pointer_cb);
    glfwSetMouseButtonCallback(window, click_cb);
    glfwSetScrollCallback(window, scroll_cb);

    lh = lib_read_file(RES_ASSET, "gamecontrollerdb.txt", (void **)&cdb, &sz);
    if (lh)
        glfwUpdateGamepadMappings(cdb);

    msg("input initialized\n");

    return 0;
}

void display_swap_buffers(void)
{
    glfwSwapBuffers(window);
    glfwPollEvents();
    /* XXX: move to the start of frame code? */
    glfw_joysticks_poll();
    joysticks_poll();
}
