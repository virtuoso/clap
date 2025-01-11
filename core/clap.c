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

struct clap_context {
    struct clap_config  cfg;
    char                **argv;
    char                **envp;
    struct timespec     current_time;
    struct phys         *phys;
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

void clap_fps_calc(struct clap_context *ctx, struct fps_data *f)
{
    bool status = false;
    struct message m;

    clock_gettime(CLOCK_MONOTONIC, &ctx->current_time);
    f->time = clap_get_current_time(ctx);
    if (!f->ts_prev.tv_sec && !f->ts_prev.tv_nsec) {
        f->ts_delta.tv_nsec = NSEC_PER_SEC / gl_refresh_rate();
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

static void clap_config_init(struct clap_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
}

static bool clap_config_is_valid(struct clap_config *cfg)
{
    if (cfg->graphics && (!cfg->frame_cb || !cfg->resize_cb || !cfg->title))
        return false;

    return true;
}

int clap_restart(struct clap_context *ctx)
{
    if (!ctx->argc || !ctx->argv)
        return -EINVAL;

    clap_done(ctx, 0);
#ifdef __APPLE__
    return execve(ctx->argv[0], ctx->argv, ctx->envp);
#else
    return execve(program_invocation_name, ctx->argv, ctx->envp);
#endif
}

struct clap_context *clap_init(struct clap_config *cfg, int argc, char **argv, char **envp)
{
    unsigned int log_flags = LOG_DEFAULT;
    struct clap_context *ctx;

    if (cfg && !clap_config_is_valid(cfg))
        return NULL;

    ctx = malloc(sizeof(*ctx));
    if (!ctx)
        return NULL;

    if (cfg)
        memcpy(&ctx->cfg, cfg, sizeof(ctx->cfg));
    else
        clap_config_init(&ctx->cfg);

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
        gl_init(ctx->cfg.title, ctx->cfg.width, ctx->cfg.height,
                ctx->cfg.frame_cb, ctx->cfg.callback_data, ctx->cfg.resize_cb);
        textures_init();
    }
    if (ctx->cfg.input)
        (void)input_init(); /* XXX: error handling */
    if (ctx->cfg.graphics && ctx->cfg.input)
        gl_debug_ui_init(ctx);
    //clap_settings = settings_init();

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
        gl_done();
    }
    if (ctx->cfg.font)
        font_done();
    messagebus_done();
    exit_cleanup_run(status);
}
