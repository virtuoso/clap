// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include "clap.h"
#include "common.h"
#include "input.h"
#include "font.h"
#include "sound.h"
#include "mesh.h"
#include "messagebus.h"
#include "librarian.h"
#include "physics.h"
#include "settings.h"
#include "ui-debug.h"
#include "util.h"

#ifdef HAVE_ASAN
const char *__asan_default_options() {
    /*
     * https://github.com/google/sanitizers/wiki/AddressSanitizerFlags
     * https://github.com/google/sanitizers/wiki/SanitizerCommonFlags
     * malloc_context_size=20
     */
    return
        "verbosity=0"
        ":check_initialization_order=true"
        ":detect_stack_use_after_return=true"
	    ":alloc_dealloc_mismatch=false"
        ":strict_string_checks=true"
        ":abort_on_error=true"
#ifndef CONFIG_BROWSER
        ":suppressions=clap.supp"
#endif /* CONFIG_BROWSER */
    ;
}
#endif /* HAVE_ASAN */

struct fps_data {
    struct timespec ts_prev, ts_delta;
    unsigned long   fps_fine, fps_coarse, seconds, count;
};

struct clap_context {
    struct clap_config  cfg;
    struct fps_data     fps;
    char                **argv;
    char                **envp;
    struct timespec     current_time;
    struct phys         *phys;
    struct settings     *settings;
    int                 argc;
};

struct phys *clap_get_phys(struct clap_context *ctx)
{
    return ctx->phys;
}

struct timespec clap_get_current_timespec(struct clap_context *ctx)
{
    return ctx->current_time;
}

double clap_get_current_time(struct clap_context *ctx)
{
    return (double)ctx->current_time.tv_sec + (double)ctx->current_time.tv_nsec / NSEC_PER_SEC;
}

static void clap_fps_calc(struct clap_context *ctx, struct fps_data *f)
{
    bool status = false;
    struct message m;

    clock_gettime(CLOCK_MONOTONIC, &ctx->current_time);
    if (!f->ts_prev.tv_sec && !f->ts_prev.tv_nsec) {
        f->ts_delta.tv_nsec = NSEC_PER_SEC / display_refresh_rate();
        f->ts_delta.tv_sec = 0;
    } else {
        timespec_diff(&f->ts_prev, &ctx->current_time, &f->ts_delta);
    }
    f->ts_prev = ctx->current_time;

    if (f->seconds != ctx->current_time.tv_sec) {
        f->fps_coarse = f->count;
        f->count      = 0;
        f->seconds    = ctx->current_time.tv_sec;
        status        = true;
    }
    f->count += 1;//f->ts_delta.tv_nsec / (1000000000/60);

    if (f->ts_delta.tv_sec) {
        f->fps_fine = 1;
    } else {
        f->fps_fine = NSEC_PER_SEC / f->ts_delta.tv_nsec;
    }

    if (status) {
        memset(&m, 0, sizeof(m));
        m.type            = MT_COMMAND;
        m.cmd.status      = 1;
        m.cmd.fps         = f->fps_fine;//f->fps_coarse;
        m.cmd.sys_seconds = f->ts_prev.tv_sec;
        message_send(&m);
    }
}

struct timespec clap_get_fps_delta(struct clap_context *ctx)
{
    return ctx->fps.ts_delta;
}

unsigned long clap_get_fps_fine(struct clap_context *ctx)
{
    return ctx->fps.fps_fine;
}

unsigned long clap_get_fps_coarse(struct clap_context *ctx)
{
    return ctx->fps.fps_coarse;
}

static void clap_settings_onload(struct settings *rs, void *data)
{
    int window_x, window_y,  window_width, window_height;
    struct clap_context *ctx = data;

    JsonNode *win_group = settings_find_get(rs, NULL, "window", JSON_OBJECT);
    if (win_group) {
        window_x = (int)settings_get_num(rs, win_group, "x");
        window_y = (int)settings_get_num(rs, win_group, "y");
        window_width = (int)settings_get_num(rs, win_group, "width");
        window_height = (int)settings_get_num(rs, win_group, "height");
        if (window_width && window_height)
            display_set_window_pos_size(window_x, window_y, window_width, window_height);
    }

    ui_debug_set_settings(rs);

    if (ctx->cfg.settings_cb)
        ctx->cfg.settings_cb(rs, ctx->cfg.settings_cb_data);
}

EMSCRIPTEN_KEEPALIVE void clap_frame(void *data)
{
    struct clap_context *ctx = data;

    clap_fps_calc(ctx, &ctx->fps);

    ctx->cfg.frame_cb(ctx->cfg.callback_data);
}

EMSCRIPTEN_KEEPALIVE void clap_resize(void *data, int width, int height)
{
    struct clap_context *ctx = data;

    ctx->cfg.resize_cb(ctx->cfg.callback_data, width, height);
}

struct settings *clap_get_settings(struct clap_context *ctx)
{
    return ctx->settings;
}

static bool clap_config_is_valid(struct clap_config *cfg)
{
    if (cfg->graphics && (!cfg->frame_cb || !cfg->resize_cb || !cfg->title))
        return false;

    return true;
}

int clap_restart(struct clap_context *ctx)
{
    int argc = ctx->argc;
    char **argv = ctx->argv;
    char **envp = ctx->envp;

    if (!argc || !argv)
        return -EINVAL;

    clap_done(ctx, 0);
#ifdef __APPLE__
    return execve(argv[0], argv, envp);
#else
    return execve(program_invocation_name, argv, envp);
#endif
}

struct clap_context *clap_init(struct clap_config *cfg, int argc, char **argv, char **envp)
{
    unsigned int log_flags = LOG_DEFAULT;
    struct clap_context *ctx;

    if (cfg && !clap_config_is_valid(cfg))
        return NULL;

    ctx = mem_alloc(sizeof(*ctx), .zero = 1);
    if (!ctx)
        return NULL;

    mesh_init();

    if (cfg)
        memcpy(&ctx->cfg, cfg, sizeof(ctx->cfg));

    if (ctx->cfg.debug)
        log_flags = LOG_FULL;
    if (ctx->cfg.quiet)
        log_flags |= LOG_QUIET;

    ctx->argc = argc;
    ctx->argv = argv;
    ctx->envp = envp;

    /* XXX: handle initialization errors */
    messagebus_init();
    log_init(log_flags);
    (void)librarian_init(ctx->cfg.base_url);
    if (ctx->cfg.font)
        font_init(ctx->cfg.default_font_name);
    if (ctx->cfg.sound)
        sound_init();
    if (ctx->cfg.phys)
        CHECK(ctx->phys = phys_init());
    if (ctx->cfg.graphics) {
        display_init(ctx->cfg.title, ctx->cfg.width, ctx->cfg.height,
                     clap_frame, ctx, clap_resize);

        textures_init();
    }
    if (ctx->cfg.input)
        (void)input_init(); /* XXX: error handling */
    if (ctx->cfg.graphics && ctx->cfg.input)
        display_debug_ui_init(ctx);
    if (ctx->cfg.settings)
        CHECK(ctx->settings = settings_init(clap_settings_onload, ctx));

    return ctx;
}

void clap_done(struct clap_context *ctx, int status)
{
    if (ctx->cfg.sound)
        sound_done();
    if (ctx->cfg.phys)
        phys_done(ctx->phys);
    if (ctx->cfg.graphics) {
        textures_done();
        display_done();
    }
    if (ctx->cfg.font)
        font_done();
    if (ctx->settings)
        settings_done(ctx->settings);
    messagebus_done();
    mem_free(ctx);
    exit_cleanup_run(status);
}
