// SPDX-License-Identifier: Apache-2.0
#include <stdarg.h>
#include <stdlib.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include "common.h"
#include "display.h"
#include "input-joystick.h"
#include "memory.h"
#include "render.h"
#include "ui-debug.h"

static int width, height;

static int refresh_rate = 0;

int display_refresh_rate(void)
{
    return refresh_rate;
}

struct calc_refresh_rate_priv {
    display_update_cb   update_fn;
    void                *update_data;
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

static void calc_refresh_rate(display_update_cb update_fn, void *data)
{
    static struct calc_refresh_rate_priv priv;

    priv.update_fn = update_fn;
    priv.update_data = data;

    emscripten_set_main_loop_arg(__calc_refresh_rate, &priv, 0, false);
    emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
}

void display_request_exit(void)
{
}

void display_title(const char *fmt, ...)
{
    LOCAL(char, title);
    va_list va;

    va_start(va, fmt);
    cres(int) res = mem_vasprintf(&title, fmt, va);
    va_end(va);
    if (!IS_CERR(res))
        emscripten_set_window_title(title);
}

void display_get_sizes(int *widthp, int *heightp)
{
    EM_ASM(window.onresize(););

    if (widthp)
        *widthp = width;
    if (heightp)
        *heightp = height;
}

void display_set_window_pos_size(int x, int y, int w, int h)
{
}

void display_get_window_pos_size(int *x, int *y, int *w, int *h)
{
    *x = *y = *w = *h = -1;
}

void display_main_loop(void)
{
}

static display_resize_cb resize_fn;
static void *callback_data;

EMSCRIPTEN_KEEPALIVE void display_resize(int w, int h)
{
    resize_fn(callback_data, w, h);
    emscripten_set_canvas_element_size("#canvas", w, h);
    width = w;
    height = h;
}

extern void www_joysticks_poll(void);
void display_swap_buffers(void)
{
    emscripten_webgl_commit_frame();
    www_joysticks_poll();
    joysticks_poll();
}

void display_enter_fullscreen(void)
{
    emscripten_request_fullscreen("#canvas", 1);
}

void display_leave_fullscreen(void)
{
    emscripten_exit_fullscreen();
}

cerr_check display_init(struct clap_context *ctx, display_update_cb update_fn, display_resize_cb rfn)
{
    EmscriptenWebGLContextAttributes attr;
    int context;

    resize_fn = rfn;
    callback_data = ctx;
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

    renderer_t *renderer = clap_get_renderer(ctx);
    renderer_init(renderer);
    renderer_set_version(renderer, 3, 0, RENDERER_ANY_PROFILE);

    EM_ASM(runtime_ready = true;);
    display_get_sizes(NULL, NULL);
    calc_refresh_rate(update_fn, ctx);

    return CERR_OK;
}

void display_debug_ui_init(struct clap_context *ctx)
{
    imgui_init(ctx, NULL, width, height);
}
