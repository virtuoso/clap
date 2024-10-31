/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CLAP_H__
#define __CLAP_CLAP_H__

struct fps_data {
    struct timespec ts_prev, ts_delta;
    unsigned long   fps_fine, fps_coarse, seconds, count;
};

struct clap_context;

void clap_fps_calc(struct clap_context *ctx, struct fps_data *f);
struct timespec clap_get_current_timespec(struct clap_context *ctx);
double clap_get_current_time(struct clap_context *ctx);

struct clap_config {
    unsigned long   debug       : 1,
                    quiet       : 1,
                    input       : 1,
                    font        : 1,
                    sound       : 1,
                    phys        : 1,
                    graphics    : 1;
    const char      *title;
    const char      *base_url;
    const char      *default_font_name;
    unsigned int    width;
    unsigned int    height;
    void            (*frame_cb)(void *data);
    void            (*resize_cb)(void *data, int width, int height);
    void            *callback_data;
};

struct clap_context *clap_init(struct clap_config *cfg, int argc, char **argv, char **envp);
void clap_done(struct clap_context *ctx, int status);
int clap_restart(struct clap_context *ctx);

#endif /* __CLAP_CLAP_H__ */
