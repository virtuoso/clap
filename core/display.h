/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_DISPLAY_H__
#define __CLAP_DISPLAY_H__

#include "config.h"

#if defined(CONFIG_BROWSER) || defined(CONFIG_GLES)
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2ext.h>
#else
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION 1
#endif /* __APPLE__ */
#include <GL/glew.h>
#endif

#include <stdbool.h>

#ifdef CONFIG_BROWSER
static inline const char *gluErrorString(int err) { return "not implemented"; }
#endif

struct clap_context;
typedef void (*display_update)(void *data);
typedef void (*display_resize)(void *data, int w, int h);
void gl_init(const char *title, int width, int height, display_update update_fn, void *update_fn_data, display_resize resize_fn);
void gl_debug_ui_init(struct clap_context *ctx);
int gl_refresh_rate(void);
void gl_main_loop(void);
void gl_done(void);
void gl_swap_buffers(void);
void gl_get_sizes(int *widthp, int *heightp);
void gl_title(const char *fmt, ...);
void gl_request_exit(void);
void gl_resize(int w, int h);
void gl_enter_fullscreen(void);
void gl_leave_fullscreen(void);
void gl_set_window_pos_size(int x, int y, int w, int h);
void gl_get_window_pos_size(int *x, int *y, int *w, int *h);

#endif /* __CLAP_DISPLAY_H__ */