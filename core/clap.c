// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
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
#include "util.h"

#ifdef HAVE_ASAN
const char *__asan_default_options() {
    /*
     * https://github.com/google/sanitizers/wiki/AddressSanitizerFlags
     * https://github.com/google/sanitizers/wiki/SanitizerCommonFlags
     * malloc_context_size=20
     */
    return
        "verbosity=1"
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
    int                 argc;
};

void clap_fps_calc(struct fps_data *f)
{
    bool status = false;
    struct timespec ts;
    struct message m;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    timespec_diff(&f->ts_prev, &ts, &f->ts_delta);
    memcpy(&f->ts_prev, &ts, sizeof(ts));

    if (f->seconds != ts.tv_sec) {
        f->fps_coarse = f->count;
        f->count      = 0;
        f->seconds    = ts.tv_sec;
        status        = true;
    }
    f->count += 1;//f->ts_delta.tv_nsec / (1000000000/60);

    if (f->ts_delta.tv_sec) {
        f->fps_fine = 1;
    } else {
        f->fps_fine = 1000000000 / f->ts_delta.tv_nsec;
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

    log_init(log_flags);
    (void)librarian_init();
    if (ctx->cfg.font)
        font_init();
    if (ctx->cfg.sound)
        sound_init();
    if (ctx->cfg.phys)
        phys_init();
    if (ctx->cfg.graphics)
        gl_init(ctx->cfg.title, ctx->cfg.width, ctx->cfg.height,
                ctx->cfg.frame_cb, ctx->cfg.callback_data, ctx->cfg.resize_cb);
    if (ctx->cfg.input)
        (void)input_init(); /* XXX: error handling */
    //clap_settings = settings_init();

    return ctx;
}

void clap_done(struct clap_context *ctx, int status)
{
    if (ctx->cfg.sound)
        sound_done();
    if (ctx->cfg.phys)
        phys_done();
    if (ctx->cfg.graphics)
        gl_done();
    exit_cleanup_run(status);
}
