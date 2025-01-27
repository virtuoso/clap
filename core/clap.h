/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CLAP_H__
#define __CLAP_CLAP_H__

struct fps_data;
struct clap_context;
struct phys;
struct settings;

/* Get the clap's physics handle */
struct phys *clap_get_phys(struct clap_context *ctx);
struct settings *clap_get_settings(struct clap_context *ctx);
struct timespec clap_get_current_timespec(struct clap_context *ctx);
double clap_get_current_time(struct clap_context *ctx);

struct timespec clap_get_fps_delta(struct clap_context *ctx);
unsigned long clap_get_fps_fine(struct clap_context *ctx);
unsigned long clap_get_fps_coarse(struct clap_context *ctx);

struct clap_config {
    unsigned long   debug       : 1,
                    quiet       : 1,
                    input       : 1,
                    font        : 1,
                    sound       : 1,
                    phys        : 1,
                    graphics    : 1,
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
};

struct clap_context *clap_init(struct clap_config *cfg, int argc, char **argv, char **envp);
void clap_done(struct clap_context *ctx, int status);
int clap_restart(struct clap_context *ctx);

#endif /* __CLAP_CLAP_H__ */
