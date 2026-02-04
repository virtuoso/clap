/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_DISPLAY_H__
#define __CLAP_DISPLAY_H__

#include <stdbool.h>
#include "config.h"
#include "error.h"


struct clap_context;
typedef void (*display_update_cb)(void *data);
typedef void (*display_resize_cb)(void *data, int w, int h);
cerr_check display_init(struct clap_context *ctx, display_update_cb update_cb, display_resize_cb resize_cb);
void display_debug_ui_init(struct clap_context *ctx);
int display_refresh_rate(void);
bool display_supports_edr(void);
void display_main_loop(void);
void display_done(void);
void display_swap_buffers(void);
void display_get_sizes(int *widthp, int *heightp);
float display_get_scale(void);
void display_title(const char *fmt, ...);
void display_request_exit(void);
void display_resize(int w, int h);
void display_enter_fullscreen(void);
void display_leave_fullscreen(void);
void display_set_window_pos_size(int x, int y, int w, int h);
void display_get_window_pos_size(int *x, int *y, int *w, int *h);

#endif /* __CLAP_DISPLAY_H__ */