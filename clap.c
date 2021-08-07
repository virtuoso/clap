#define _GNU_SOURCE
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include "clap.h"
#include "common.h"
#include "input.h"
#include "messagebus.h"
#include "librarian.h"
#include "settings.h"
#include "util.h"

#if defined(__has_feature)
# if __has_feature(address_sanitizer)
const char *__asan_default_options() {
    /*
     * https://github.com/google/sanitizers/wiki/AddressSanitizerFlags
     * https://github.com/google/sanitizers/wiki/SanitizerCommonFlags
     * malloc_context_size=20
     */
    return "verbosity=1:check_initialization_order=true:detect_stack_use_after_return=true:"
	   "alloc_dealloc_mismatch=true:strict_string_checks=true:abort_on_error=true";
}
# endif /* __has_feature(address_sanitizer) */
#endif /* __has_feature */

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
    f->count++;

    if (f->ts_delta.tv_sec) {
        f->fps_fine = 1;
    } else {
        f->fps_fine = 1000000000 / f->ts_delta.tv_nsec;
    }

    if (status) {
        memset(&m, 0, sizeof(m));
        m.type            = MT_COMMAND;
        m.cmd.status      = 1;
        m.cmd.fps         = f->fps_coarse;
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
    return true;
}

static int clap_argc;
static char **clap_argv;
static char **clap_envp;

int clap_restart(void)
{
    if (!clap_argc || !clap_argv)
        return -EINVAL;

    clap_done(0);
#ifdef __APPLE__
    return execve(clap_argv[0], clap_argv, clap_envp);
#else
    return execve(program_invocation_name, clap_argv, clap_envp);
#endif
}

int clap_init(struct clap_config *cfg, int argc, char **argv, char **envp)
{
    unsigned int log_flags = LOG_DEFAULT;
    struct clap_config config;

    if (cfg) {
        if (!clap_config_is_valid(cfg))
            return -EINVAL;
        memcpy(&config, cfg, sizeof(config));
    } else {
        clap_config_init(&config);
    }

    if (config.debug)
        log_flags = LOG_FULL;
    if (config.quiet)
        log_flags |= LOG_QUIET;

    clap_argc = argc;
    clap_argv = argv;
    clap_envp = envp;

    log_init(log_flags);
    (void)librarian_init();
    //clap_settings = settings_init();

    return 0;
}

void clap_done(int status)
{
    exit_cleanup_run(status);
}
