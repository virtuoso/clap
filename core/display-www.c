// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include "common.h"
#include "display.h"
#include "input-joystick.h"
#include "ui-debug.h"

static int width, height;

bool gl_does_vao(void)
{
    return false;
}

static int refresh_rate = 0;

int gl_refresh_rate(void)
{
    return refresh_rate;
}

struct calc_refresh_rate_priv {
    display_update  update_fn;
    void            *update_data;
};

#define AVG_FRAMES 20
#define SKIP_FRAMES 2
EMSCRIPTEN_KEEPALIVE void __calc_refresh_rate(void *data)
{
    static struct timespec ts_start, ts_end, ts_delta;
    struct calc_refresh_rate_priv *priv = data;
    static long total = 0;
    static int frame = 0;

    if (frame < SKIP_FRAMES) {
        frame++;
        return;
    } else if (frame == SKIP_FRAMES) {
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        frame++;
        return;
    } else if (frame < AVG_FRAMES + SKIP_FRAMES) {
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        timespec_diff(&ts_start, &ts_end, &ts_delta);
        ts_start = ts_end;
        total += ts_delta.tv_nsec;
        frame++;
        return;
    }

    total /= AVG_FRAMES - 1;
    refresh_rate = 1000000000L / total;
    dbg("Estimated RAF refresh rate: %d\n", refresh_rate);
    emscripten_cancel_main_loop();
    emscripten_set_main_loop_arg(priv->update_fn, priv->update_data, 0, false);
    emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
}

static void calc_refresh_rate(display_update update_fn, void *data)
{
    static struct calc_refresh_rate_priv priv;

    priv.update_fn = update_fn;
    priv.update_data = data;

    emscripten_set_main_loop_arg(__calc_refresh_rate, &priv, 0, false);
    emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
}

void gl_request_exit(void)
{
}

void gl_title(const char *fmt, ...)
{
    LOCAL(char, title);
    va_list va;

    va_start(va, fmt);
    vasprintf(&title, fmt, va);
    va_end(va);
    EM_ASM(document.title = UTF8ToString($0);, title);
}

void gl_get_sizes(int *widthp, int *heightp)
{
    EM_ASM(window.onresize(););

    if (widthp)
        *widthp = width;
    if (heightp)
        *heightp = height;
}

void gl_main_loop(void)
{
}

static display_resize resize_fn;
static void *callback_data;

EMSCRIPTEN_KEEPALIVE void gl_resize(int w, int h)
{
    resize_fn(callback_data, w, h);
    width = w;
    height = h;
}

extern void www_joysticks_poll(void);
extern void www_touch_poll(void);
void gl_swap_buffers(void)
{
    emscripten_webgl_commit_frame();
    www_joysticks_poll();
    www_touch_poll();
    joysticks_poll();
}

void gl_enter_fullscreen(void)
{
    emscripten_request_fullscreen("#canvas", 1);
}

void gl_leave_fullscreen(void)
{
    emscripten_exit_fullscreen();
}

void gl_init(const char *title, int width, int height, display_update update_fn, void *data, display_resize rfn)
{
    EmscriptenWebGLContextAttributes attr;
    const unsigned char *exts;
    int context;

    resize_fn = rfn;
    callback_data = data;
    emscripten_webgl_init_context_attributes(&attr);
    attr.explicitSwapControl       = 0;
    attr.alpha                     = 1;
    attr.depth                     = 1;
    attr.stencil                   = 1;
    attr.antialias                 = 1;
    attr.majorVersion              = 2;
    attr.minorVersion              = 0;
    attr.enableExtensionsByDefault = 1;
    
    context = emscripten_webgl_create_context("#canvas", &attr);

    emscripten_webgl_make_context_current(context);
    exts = glGetString(GL_EXTENSIONS);
    msg("GL context: %d Extensions: '%s'\n", context, exts);
    EM_ASM(runtime_ready = true;);
    gl_get_sizes(NULL, NULL);
    calc_refresh_rate(update_fn, data);
    //resize_fn(width, height);
}

void gl_debug_ui_init(void)
{
    imgui_init(NULL, width, height);
}
