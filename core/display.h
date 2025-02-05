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
#include "error.h"

#ifdef CONFIG_BROWSER
static inline const char *gluErrorString(int err) { return "not implemented"; }
#endif

struct clap_context;
typedef void (*display_update_cb)(void *data);
typedef void (*display_resize_cb)(void *data, int w, int h);
cerr_check display_init(struct clap_context *ctx, display_update_cb update_cb, display_resize_cb resize_cb);
void display_debug_ui_init(struct clap_context *ctx);
int display_refresh_rate(void);
void display_main_loop(void);
void display_done(void);
void display_swap_buffers(void);
void display_get_sizes(int *widthp, int *heightp);
void display_title(const char *fmt, ...);
void display_request_exit(void);
void display_resize(int w, int h);
void display_enter_fullscreen(void);
void display_leave_fullscreen(void);
void display_set_window_pos_size(int x, int y, int w, int h);
void display_get_window_pos_size(int *x, int *y, int *w, int *h);

#endif /* __CLAP_DISPLAY_H__ */