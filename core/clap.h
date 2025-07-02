/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CLAP_H__
#define __CLAP_CLAP_H__

#include "lut.h"
#include "render.h"

/* Clap build options string */
const char *clap_build_options(void);

struct fps_data;
typedef struct clap_context clap_context;
struct phys;
struct settings;
typedef struct sound_context sound_context;
typedef struct font_context font_context;
typedef struct shader_context shader_context;
typedef struct render_options render_options;

typedef struct clap_os_info {
    char    *name;
    bool    mobile;
} clap_os_info;

/* Get OS info */
clap_os_info *clap_get_os(clap_context *ctx);
/* Get the clap's physics handle */
struct phys *clap_get_phys(struct clap_context *ctx) __nonnull_params((1));
/* Get clap's config structure */
struct clap_config *clap_get_config(struct clap_context *ctx) __returns_nonnull __nonnull_params((1));
/* Get clap's renderer */
renderer_t *clap_get_renderer(struct clap_context *ctx) __returns_nonnull __nonnull_params((1));
/* Get clap's render options (mutable) */
render_options *clap_get_render_options(struct clap_context *ctx) __returns_nonnull __nonnull_params((1));
/* Get clap's shaders context */
shader_context *clap_get_shaders(struct clap_context *ctx) __nonnull_params((1));
/* Get clap's UI handle */
struct ui *clap_get_ui(clap_context *ctx) __returns_nonnull __nonnull_params((1));
/* Get clap's font handle */
font_context *clap_get_font(clap_context *ctx) __nonnull_params((1));
sound_context *clap_get_sound(struct clap_context *ctx) __nonnull_params((1));
struct settings *clap_get_settings(struct clap_context *ctx) __nonnull_params((1));
struct timespec clap_get_current_timespec(struct clap_context *ctx) __nonnull_params((1));
double clap_get_current_time(struct clap_context *ctx) __nonnull_params((1));

typedef void (*clap_timer_fn)(void *data);
typedef struct clap_timer clap_timer;
cresp_ret(clap_timer);

cresp(clap_timer) clap_timer_set(clap_context *ctx, double dt, clap_timer *timer,
                                 clap_timer_fn fn, void *data) __nonnull_params((1));
void clap_timer_cancel(clap_context *ctx, clap_timer *timer) __nonnull_params((1, 2));

struct timespec clap_get_fps_delta(struct clap_context *ctx) __nonnull_params((1));
unsigned long clap_get_fps_fine(struct clap_context *ctx) __nonnull_params((1));
unsigned long clap_get_fps_coarse(struct clap_context *ctx) __nonnull_params((1));

struct list *clap_lut_list(clap_context *ctx) __nonnull_params((1));
cresp(lut) clap_lut_find(clap_context *ctx, const char *name) __nonnull_params((1, 2));

struct clap_config {
    unsigned long   debug       : 1,
                    quiet       : 1,
                    input       : 1,
                    font        : 1,
                    sound       : 1,
                    phys        : 1,
                    graphics    : 1,
                    ui          : 1,
                    settings    : 1;
    const char      *title;
    const char      *base_url;
    const char      *default_font_name;
    unsigned int    width;
    unsigned int    height;
    void            (*frame_cb)(void *data);
    void            (*resize_cb)(void *data, int width, int height);
    void            *callback_data;
    void            (*settings_cb)(struct settings *rs, void *data);
    void            *settings_cb_data;
    lut_preset      *lut_presets;
};

cresp_struct_ret(clap_context);

cresp_check(clap_context) clap_init(struct clap_config *cfg, int argc, char **argv, char **envp);
void clap_done(struct clap_context *ctx, int status) __nonnull_params((1));
cres(int) clap_restart(struct clap_context *ctx) __nonnull_params((1));

#endif /* __CLAP_CLAP_H__ */
