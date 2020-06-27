#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include "common.h"
#include "display.h"

static int width, height;

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

EMSCRIPTEN_KEEPALIVE void gl_resize(int w, int h)
{
    resize_fn(w, h);
    width = w;
    height = h;
}

void gl_swap_buffers(void)
{
    emscripten_webgl_commit_frame();
}

void gl_enter_fullscreen(void)
{
    emscripten_request_fullscreen(EMSCRIPTEN_EVENT_TARGET_WINDOW, 1);
}

void gl_leave_fullscreen(void)
{
    emscripten_exit_fullscreen();
}

void gl_init(const char *title, int width, int height, display_update update_fn, display_resize rfn)
{
    EmscriptenWebGLContextAttributes attr;
    const unsigned char *exts;
    int context;

    resize_fn = rfn;
    emscripten_webgl_init_context_attributes(&attr);
    attr.explicitSwapControl       = 0;
    attr.alpha                     = 1;
    attr.depth                     = 1;
    attr.stencil                   = 1;
    attr.antialias                 = 1;
    attr.majorVersion              = 1; /* Safari doesn't do WebGL 2 */
    attr.minorVersion              = 0;
    attr.enableExtensionsByDefault = 1;
    
    context = emscripten_webgl_create_context("#canvas", &attr);

    emscripten_webgl_make_context_current(context);
    exts = glGetString(GL_EXTENSIONS);
    msg("GL context: %d Extensions: '%s'\n", context, exts);
    gl_get_sizes(NULL, NULL);
    //resize_fn(width, height);
}
