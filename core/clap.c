// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include "clap.h"
#include "common.h"
#include "display.h"
#include "input.h"
#include "lut.h"
#include "font.h"
#include "networking.h"
#include "pipeline-builder.h"
#include "profiler.h"
#include "render.h"
#include "scene.h"
#include "sound.h"
#include "mesh.h"
#include "messagebus.h"
#include "librarian.h"
#include "physics.h"
#include "settings.h"
#include "ui.h"
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
#if defined(__APPLE__) && defined(CONFIG_RENDERER_OPENGL)
        ":strict_string_checks=false"
#else
        ":strict_string_checks=true"
#endif /* !(__APPLE__ && CONFIG_RENDERER_OPENGL) */
        ":abort_on_error=true"
#ifndef CONFIG_BROWSER
        ":suppressions=clap.supp"
#endif /* CONFIG_BROWSER */
    ;
}
#endif /* HAVE_ASAN */

#if defined(__has_feature)
# if __has_feature(undefined_behavior_sanitizer)
#  define UBSAN_ENABLED 1
# else /* __has_feature(undefined_behavior_sanitizer) */
#  define UBSAN_ENABLED 0
# endif /* !__has_feature(undefined_behavior_sanitizer) */
#else /* __has_feature */
# ifdef _WIN32
#  define UBSAN_ENABLED 0
# else /* _WIN32 */
void weak __ubsan_handle_builtin_unreachable(void *x);
#  define UBSAN_ENABLED (!!&__ubsan_handle_builtin_unreachable)
# endif /* !_WIN32 */
#endif /* !__has_feature */

const char *clap_build_options(void)
{
    static char str[128];

    snprintf(str, sizeof(str),
#ifdef HAVE_ASAN
        "[asan]"
#endif /* HAVE_ASAN */
        "%s", UBSAN_ENABLED ? "[ubsan]" : ""
    );

    return str;
}

struct fps_data {
    struct timespec ts_prev, ts_delta;
    unsigned long   fps_fine, fps_coarse, seconds, count;
};

typedef struct clap_timer {
    struct list     entry;
    clap_timer_fn   fn;
    void            *priv;
    double          time;
} clap_timer;

typedef struct clap_context {
    struct clap_config  cfg;
    struct fps_data     fps;
    char                **argv;
    char                **envp;
    clap_os_info        os_info;
    struct timespec     current_time;
    sound_context       *sound;
    font_context        *font;
    struct phys         *phys;
    struct settings     *settings;
    renderer_t          renderer;
    render_options      render_options;
    render_options      render_options_current;
    shader_context      *shaders;
    struct scene        scene;
    pipeline            *pl;
    struct list         luts;
    struct list         timers;
    struct ui           ui;
    messagebus          mb;
    int                 argc;
    int                 exit_after;
    bool                fullscreen;
    bool                paused;
} clap_context;

/****************************************************************************
 * Context accessors
 ****************************************************************************/

struct clap_config *clap_get_config(struct clap_context *ctx)
{
    return &ctx->cfg;
}

messagebus *clap_get_messagebus(struct clap_context *ctx)
{
    return &ctx->mb;
}

renderer_t *clap_get_renderer(struct clap_context *ctx)
{
    if (!ctx->cfg.graphics) return NULL;
    return &ctx->renderer;
}

void clap_get_viewport(struct clap_context *ctx, int *px, int *py, int *pw, int *ph)
{
    if (!ctx->cfg.graphics) return;
    renderer_get_viewport(&ctx->renderer, px, py, pw, ph);
}

struct scene *clap_get_scene(struct clap_context *ctx)
{
    if (!ctx->cfg.graphics) return NULL;
    return &ctx->scene;
}

render_options *clap_get_render_options(struct clap_context *ctx)
{
    return &ctx->render_options;
}

shader_context *clap_get_shaders(struct clap_context *ctx)
{
    return ctx->shaders;
}

pipeline *clap_get_pipeline(struct clap_context *ctx)
{
    return ctx->pl;
}

struct ui *clap_get_ui(clap_context *ctx)
{
    return &ctx->ui;
}

struct settings *clap_get_settings(struct clap_context *ctx)
{
    return ctx->settings;
}

struct phys *clap_get_phys(struct clap_context *ctx)
{
    return ctx->phys;
}

sound_context *clap_get_sound(struct clap_context *ctx)
{
    return ctx->sound;
}

font_context *clap_get_font(clap_context *ctx)
{
    return ctx->font;
}

clap_os_info *clap_get_os(clap_context *ctx)
{
    return &ctx->os_info;
}

struct timespec clap_get_current_timespec(struct clap_context *ctx)
{
    return ctx->current_time;
}

double clap_get_current_time(struct clap_context *ctx)
{
    /*
     * If clap_timer_set() is called before ctx->current time is first set,
     * it'll go off immediately. Prevent that.
     */
    if (unlikely(!ctx->current_time.tv_nsec && !ctx->current_time.tv_sec))
        clock_gettime(CLOCK_MONOTONIC, &ctx->current_time);

    return (double)ctx->current_time.tv_sec + (double)ctx->current_time.tv_nsec / NSEC_PER_SEC;
}

static void clap_fps_calc(struct clap_context *ctx, struct fps_data *f)
{
    bool status = false;
    struct message m;

    clock_gettime(CLOCK_MONOTONIC, &ctx->current_time);
    if (!f->ts_prev.tv_sec && !f->ts_prev.tv_nsec) {
        // Default to a reasonable value (~60 FPS) instead of display_refresh_rate()
        f->ts_delta.tv_nsec = NSEC_PER_SEC / 1000 * 16;  // 16ms
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
    f->count += 1;

    /* More stable FPS calculation */
    f->fps_fine = f->ts_delta.tv_sec ? 1 : f->ts_delta.tv_nsec ? (NSEC_PER_SEC / f->ts_delta.tv_nsec) : 1;

    if (status) {
        memset(&m, 0, sizeof(m));
        m.type            = MT_COMMAND;
        m.cmd.status      = 1;
        m.cmd.fps         = f->fps_fine;
        m.cmd.sys_seconds = f->ts_prev.tv_sec;
        message_send(ctx, &m);
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

bool clap_is_paused(clap_context *ctx)
{
    return ctx->paused;
}

/****************************************************************************
 * Render options' defaults
 ****************************************************************************/

static void clap_init_render_options(struct clap_context *ctx)
{
    ctx->render_options.shadow_vsm = true;
    ctx->render_options.shadow_msaa = false;
    ctx->render_options.laplace_kernel = 3;
    ctx->render_options.edge_antialiasing = true;
    ctx->render_options.shadow_outline = true;
    ctx->render_options.shadow_outline_threshold = 0.4;
    ctx->render_options.hdr = true;
    /*
     * Apple Silicon's GL driver can't handle this much postprocessing
     * without driving FPS into single digits and heating up like a frying
     * pan. Disable it until Metal renderer is ready.
     */
#if !(defined(__APPLE__) && defined(CONFIG_RENDERER_OPENGL))
    ctx->render_options.ssao = true;
#endif /* __APPLE__ && __arm64__ */
    ctx->render_options.ssao_radius = 0.3;
    ctx->render_options.ssao_weight = 0.85;
    ctx->render_options.bloom_exposure = 1.7;
    ctx->render_options.bloom_intensity = 2.0;
    ctx->render_options.bloom_threshold = 0.27;
    ctx->render_options.bloom_operator = 1.0;
    ctx->render_options.lighting_exposure = 1.3;
    ctx->render_options.lighting_operator = 0.0;
    ctx->render_options.contrast = 0.15;
    ctx->render_options.fog_near = 5.0;
    ctx->render_options.fog_far = 80.0;
    vec3_dup(ctx->render_options.fog_color, (vec3){ 0.11, 0.14, 0.03 });
}

/****************************************************************************
 * Timers API
 ****************************************************************************/

/*
 * Set a timer at a given interval @dt (seconds) to call function @fn with
 * parameter @data. If @timer is non-NULL, a new timer is allocated, otherwise
 * the existing one will be reused.
 */
cresp(clap_timer) clap_timer_set(clap_context *ctx, double dt, clap_timer *timer,
                                 clap_timer_fn fn, void *data)
{
    if (dt < 0.0 || !fn)
        return cresp_error(clap_timer, CERR_INVALID_ARGUMENTS);

    if (!timer) {
        timer = mem_alloc(sizeof(clap_timer));
        if (!timer)
            return cresp_error(clap_timer, CERR_NOMEM);
    }

    double end = clap_get_current_time(ctx) + dt;

    clap_timer *iter = NULL;
    /* Keep the list of timers sorted by time */
    list_for_each_entry(iter, &ctx->timers, entry)
        if (iter->time > end)
            break;

    if (list_empty(&ctx->timers) || iter == list_first_entry(&ctx->timers, clap_timer, entry))
        list_prepend(&ctx->timers, &timer->entry);
    else
        list_prepend(&iter->entry, &timer->entry);
    timer->fn = fn;
    timer->priv = data;
    timer->time = end;

    return cresp_val(clap_timer, timer);
}

/* Cancel a given timer */
void clap_timer_cancel(clap_context *ctx, clap_timer *timer)
{
    if (list_empty(&timer->entry)) {
        err("deleting timer while timer list is empty\n");
        return;
    }

    list_del(&timer->entry);
    mem_free(timer);
}

static void clap_timers_run(clap_context *ctx)
{
    double time = clap_get_current_time(ctx);
    clap_timer *timer, *iter;
    DECLARE_LIST(fire);

    /*
     * Move timers that are going off to a local list before running
     * their callbacks, as they're likely to re-arm themselves, which
     * will re-insert them into ctx::timers; this avoids the potential
     * runaway iterator, if that happens.
     */
    list_for_each_entry_iter(timer, iter, &ctx->timers, entry)
        if (timer->time <= time) {
            list_del(&timer->entry);
            list_append(&fire, &timer->entry);
        } else {
            break;
        }

    list_for_each_entry_iter(timer, iter, &fire, entry) {
        timer->fn(timer->priv);
        /* Didn't re-arm itself, remove it */
        if (timer->time <= time)
            clap_timer_cancel(ctx, timer);
    }
}

static void clap_timers_done(clap_context *ctx)
{
    clap_timer *timer, *iter;

    list_for_each_entry_iter(timer, iter, &ctx->timers, entry)
        clap_timer_cancel(ctx, timer);
}

/****************************************************************************
 * Rendering pipeline
 ****************************************************************************/

/* Table of boolean settings that if changed require a pipeline rebuild */
static const struct rebuild_reason {
    const char  *name;
    size_t      offset;
} rebuild_reason[] = {
    { .name = "shadow_msaa",        .offset = offsetof(render_options, shadow_msaa) },
    { .name = "model_msaa",         .offset = offsetof(render_options, model_msaa) },
    { .name = "edge_sobel",         .offset = offsetof(render_options, edge_sobel) },
    { .name = "ssao",               .offset = offsetof(render_options, ssao) },
    { .name = "shadow_vsm",         .offset = offsetof(render_options, shadow_vsm) },
    { .name = "edge_antialiasing",  .offset = offsetof(render_options, edge_antialiasing) },
};

static cerr build_main_pl(clap_context *ctx)
{
    auto scene = clap_get_scene(ctx);
    ctx->pl = CRES_RET_CERR(
        pipeline_build(&(pipeline_builder_opts) {
            .pl_opts    = &(pipeline_init_options) {
                .clap_ctx       = ctx,
                .light          = &scene->light,
                .camera         = &scene->cameras[0],
                .name           = "main"
            },
            .mq         = &scene->mq,
            .pl         = ctx->pl
        })
    );

    memcpy(&ctx->render_options_current, &ctx->render_options, sizeof(render_options));

    return CERR_OK;
}

static cerr rebuild_pl_if_needed(clap_context *ctx)
{
    render_options *ropts = &ctx->render_options;
    render_options *ropts_current = &ctx->render_options_current;

    bool rebuild_pl = false;
    for (size_t i = 0; i < array_size(rebuild_reason); i++) {
        bool *a = (bool *)((void *)ropts + rebuild_reason[i].offset);
        bool *b = (bool *)((void *)ropts_current + rebuild_reason[i].offset);
        if (*a != *b) {
            rebuild_pl = true;
            dbg("pipeline rebuild reason: %s: %d -> %d\n", rebuild_reason[i].name, *b, *a);
            *b = *a;
        }
    }

    if (rebuild_pl) {
         pipeline_clearout(ctx->pl);
        return build_main_pl(ctx);
    }

    return CERR_OK;
}

/****************************************************************************
 * Main callbacks
 ****************************************************************************/

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
        ctx->cfg.settings_cb(ctx, rs, ctx->cfg.settings_cb_data);
}

EMSCRIPTEN_KEEPALIVE void clap_frame(void *data)
{
    struct clap_context *ctx = data;
    struct ui *ui = clap_get_ui(ctx);

    mem_frame_begin();
    clap_fps_calc(ctx, &ctx->fps);
    clap_timers_run(ctx);

    if (ctx->fullscreen) {
        ctx->fullscreen = false;
        display_enter_fullscreen();
    }

    renderer_frame_begin(&ctx->renderer);

    int width, height;
    renderer_get_viewport(&ctx->renderer, NULL, NULL, &width, &height);

    imgui_render_begin(width, height);
    fuzzer_input_step(ctx);
    input_events_dispatch();

    PROF_FIRST(start);

    struct scene *scene = clap_get_scene(ctx);
    if (scene->control) {
        /*
         * calls into character_move(): handle inputs, adjust velocities etc
         * for the physics step's dynamics simulation (dWorldStep())
         */
        scene_characters_move(scene);
    } else {
        double dt = clap_get_fps_delta(ctx).tv_nsec / (double)NSEC_PER_SEC;
        float lin_speed = scene->lin_speed * dt;

        /* Always compute the active inputs in the frame */
        motion_compute(&scene->mctl, scene->camera, lin_speed * 4.0);

        transform_move(&scene->camera->xform, (vec3){ scene->mctl.dx, 0.0, scene->mctl.dz });
        transform_set_updated(&scene->camera->xform);
    }

    PROF_STEP(move, start)

    double dt = ctx->fps.ts_delta.tv_nsec / (double)NSEC_PER_SEC;
    phys_step(clap_get_phys(ctx), dt);

    PROF_STEP(phys, move);

#ifdef CONFIG_NETWORKING
    if (ctx->cfg.networking)    networking_poll();
#endif /* CONFIG_NETWORKING */

    PROF_STEP(net, phys);

    scene_update(scene);

    scene_cameras_calc(scene);

    ui_update(ui);

    PROF_STEP(updates, net);

    if (ctx->cfg.frame_cb)
        ctx->cfg.frame_cb(ctx, ctx->cfg.callback_data);

    PROF_STEP(callback, updates);

    pipeline_render(ctx->pl, clap_is_paused(ctx) ? 1 : 0);
    rebuild_pl_if_needed(ctx);

    PROF_STEP(scene_render, callback);

    models_render(ui->renderer, &ui->mq);

    PROF_STEP(ui_render, scene_render);

    profiler_show(PROF_PTR(start), ctx->fps.fps_fine);
    renderer_debug(&ctx->renderer);
    pipeline_debug(ctx->pl);
    input_debug();
    controllers_debug();
    memory_debug();

    imgui_render();
    renderer_frame_end(&ctx->renderer);
    display_swap_buffers();

    mem_frame_end();
}

EMSCRIPTEN_KEEPALIVE void clap_resize(void *data, int width, int height)
{
    struct clap_context *ctx = data;

    if (ctx->settings) {
        int window_x, window_y, window_width, window_height;
        display_get_window_pos_size(&window_x, &window_y, &window_width, &window_height);
        JsonNode *win_group = settings_find_get(ctx->settings, NULL, "window", JSON_OBJECT);
        if (win_group) {
            settings_set_num(ctx->settings, win_group, "x", window_x);
            settings_set_num(ctx->settings, win_group, "y", window_y);
            settings_set_num(ctx->settings, win_group, "width", window_width);
            settings_set_num(ctx->settings, win_group, "height", window_height);
        }
    }

    renderer_viewport(&ctx->renderer, 0, 0, width, height);

    struct ui *ui = clap_get_ui(ctx);
    ui->width  = width;
    ui->height = height;

    message_input_send(
        ctx,
        &(struct message_input) {
            .resize = 1,
            .x      = width,
            .y      = height,
        },
        NULL
    );

    if (ctx->pl)    pipeline_resize(ctx->pl, width, height);

    if (ctx->cfg.resize_cb)
        ctx->cfg.resize_cb(ctx, ctx->cfg.callback_data, width, height);

    touch_input_set_size(width, height);
}

/****************************************************************************
 * Color grading LUT API
 ****************************************************************************/

struct list *clap_lut_list(clap_context *ctx)
{
    return &ctx->luts;
}

cresp(lut) clap_lut_find(clap_context *ctx, const char *name)
{
    return lut_find(&ctx->luts, name);
}

cerr clap_set_lighting_lut(clap_context *ctx, const char *name)
{
    clap_get_render_options(ctx)->lighting_lut = CRES_RET_CERR(
        clap_lut_find(ctx, name)
    );

    return CERR_OK;
}

static cerr clap_lut_generate(clap_context *ctx, lut_preset *presets, unsigned int side)
{
    if (!ctx->cfg.graphics)
        return CERR_INVALID_OPERATION;

    if (!presets)
        return CERR_INVALID_ARGUMENTS;

    for (int i = 0; presets[i] < LUT_MAX; i++) {
        CRES_RET(
            lut_generate(&ctx->renderer, &ctx->luts, presets[i], side),
            { luts_done(&ctx->luts); return cerr_error_cres(__resp); }
        );
    }

    return CERR_OK;
}

static bool clap_config_is_valid(struct clap_config *cfg)
{
    if (cfg->graphics && (!cfg->frame_cb || !cfg->title))
        return false;
    if (cfg->ui && !cfg->graphics)
        return false;
    if (cfg->lut_presets && !cfg->graphics)
        return false;

    return true;
}

/****************************************************************************
 * OS detection
 ****************************************************************************/
#ifdef __EMSCRIPTEN__
static char *__user_agent;

EMSCRIPTEN_KEEPALIVE void clap_set_user_agent(const char *user_agent)
{
    __user_agent = strdup(user_agent);
}

static cerr clap_os_init(struct clap_context *ctx)
{
    EM_ASM(
        ccall("clap_set_user_agent", 'void', ['string'], [navigator.userAgent]);
    );

    if (!__user_agent)
        return CERR_NOT_SUPPORTED;

    ctx->os_info.name = strdup(__user_agent);
    free(__user_agent);
    __user_agent = NULL;

    if (!ctx->os_info.name)
        return CERR_NOMEM;

    if (ctx->os_info.name &&
        (strstr(ctx->os_info.name, "iPhone") ||
         strstr(ctx->os_info.name, "iPad")
        )
    )
        ctx->os_info.mobile = true;

    return CERR_OK;
}
#else
static cerr clap_os_init(struct clap_context *ctx)
{
    /* XXX: get struct uts_name from uname() */
    ctx->os_info.name = strdup(
#if defined(linux)
        "Linux"
#elif defined(__APPLE__)
        "Mac OS X"
#else /* XXX: windows */
        "unknown"
#endif
    );

    return ctx->os_info.name ? CERR_OK : CERR_NOMEM;
}
#endif /* __EMSCRIPTEN__ */

/****************************************************************************
 * Main API
 ****************************************************************************/

static int clap_handle_command(struct clap_context *ctx, struct message *m, void *data)
{
    if (m->type != MT_COMMAND)  return MSG_HANDLED;

    if (m->cmd.toggle_modality) ctx->paused = !ctx->paused;

    if (m->cmd.status && ctx->exit_after >= 0 && !--ctx->exit_after)
        display_request_exit();

    return MSG_HANDLED;
}

cres(int) clap_restart(struct clap_context *ctx)
{
    int argc = ctx->argc;
    char **argv = ctx->argv;
    char **envp = ctx->envp;

    if (!argc || !argv)
        return cres_error(int, CERR_INVALID_ARGUMENTS);

    clap_done(ctx, 0);
#ifdef __APPLE__
    return cres_val(int, execve(argv[0], argv, envp));
#else
    return cres_val(int, execve(program_invocation_name, argv, envp));
#endif
}

static cerr handle_server_opt(clap_context *ctx, const char *optarg)
{
    if (!ctx->cfg.networking)
        return CERR_NOT_SUPPORTED_REASON(.fmt = "server option with networking disabled");

    ctx->cfg.networking->server_ip = optarg;

    return CERR_OK;
}

static cerr handle_help_opt(clap_context *ctx, const char *optarg);

const char *clap_get_argv(clap_context *ctx, int idx)
{
    if (ctx->argc <= idx)   return NULL;
    return ctx->argv[idx];
}

typedef enum {
    CLI_NONE = 0,
    CLI_BOOL,
    CLI_INT,
    CLI_LONG,
    CLI_STR,
} cli_opt_type;

static const struct clap_cli_options_desc {
    const char      *help;
    const char      *arg_help;
    const char      *long_name;
    char            short_name;
    bool            arg_required;
    cli_opt_type    type;
    size_t          ctx_offset;
    void            *ptr;
    cerr            (*handle)(clap_context *ctx, const char *optarg);
} clap_cli_options_desc[CLAP_CLI_SENTINEL] = {
    [CLAP_CLI_HELP_BIT] = {
        .long_name      = "help",
        .short_name     = 'h',
        .help           = "print this help message",
        .handle         = handle_help_opt
    },
    [CLAP_CLI_FULLSCREEN_BIT] = {
        .long_name      = "fullscreen",
        .short_name     = 'F',
        .help           = "start in fullscreen",
        .type           = CLI_BOOL,
        .ctx_offset     = offsetof(clap_context, fullscreen)
    },
    [CLAP_CLI_EXITAFTER_BIT] = {
        .long_name      = "exitafter",
        .short_name     = 'e',
        .help           = "exit after <seconds>",
        .arg_help       = "seconds",
        .arg_required   = true,
        .type           = CLI_INT,
        .ctx_offset     = offsetof(clap_context, exit_after)
    },
    [CLAP_CLI_ABORT_ON_ERROR_BIT] = {
        .long_name      = "aoe",
        .short_name     = 'E',
        .help           = "abort on error",
        .type           = CLI_BOOL,
        .ptr            = &abort_on_error
    },
    [CLAP_CLI_SERVER_ADDR_BIT] = {
        .long_name      = "server",
        .short_name     = 'S',
        .help           = "IP address of the server to use",
        .arg_help       = "IP",
        .arg_required   = true,
        .type           = CLI_STR,
        .handle         = handle_server_opt
    },
};

#define HELP_OPT_PFX "  "
static cerr handle_help_opt(clap_context *ctx, const char *optarg)
{
    if (optarg) err("Unrecognized option '%s'\n", optarg);

    msg("Usage: %s [OPTIONS]\n\nOptions:\n", str_basename(ctx->argv[0]));
    for (size_t i = 0; i < array_size(clap_cli_options_desc); i++) {
        if (!(ctx->cfg.cli_opts & (1u << i)))   continue;

        auto desc = &clap_cli_options_desc[i];

        char help[256];
        size_t len = 0;

        if (desc->short_name)
            len += snprintf(help + len, sizeof(help) - len, "%s-%c", HELP_OPT_PFX, desc->short_name);
        if (desc->arg_help)
            len += snprintf(help + len, sizeof(help) - len, " <%s>", desc->arg_help);
        if (desc->long_name)
            len += snprintf(help + len, sizeof(help) - len, "%s--%s", len ? ", " : HELP_OPT_PFX, desc->long_name);
        if (desc->arg_help)
            len += snprintf(help + len, sizeof(help) - len, " <%s>", desc->arg_help);
        if (desc->help)
            len += snprintf(help + len, sizeof(help) - len, "\n%s%s%s", HELP_OPT_PFX, HELP_OPT_PFX, desc->help);

        msg("%s\n\n", help);
    }

    return optarg ? CERR_INVALID_ARGUMENTS : CERR_REQUEST_EXIT;
}

static cerr clap_cli_opts_process(clap_context *ctx)
{
    struct option long_options[CLAP_CLI_SENTINEL + 1];
    char short_options[CLAP_CLI_SENTINEL * 2 + 1];

    size_t lopts_idx = 0, sopts_idx = 0;

    /* set up getopt's arrays */
    for (int i = 0; i < CLAP_CLI_SENTINEL; i++) {
        if (!(ctx->cfg.cli_opts & (1u << i)))   continue;

        auto desc = &clap_cli_options_desc[i];
        long_options[lopts_idx++] = (struct option) {
            desc->long_name,
            desc->arg_required ? required_argument : no_argument,
            NULL,
            desc->short_name
        };

        short_options[sopts_idx++] = desc->short_name;
        if (desc->arg_required) short_options[sopts_idx++] = ':';
    }

    long_options[lopts_idx] = (struct option) {};
    short_options[sopts_idx] = 0;

    int option_index;

    for (;;) {
next:
        int c = getopt_long(ctx->argc, ctx->argv, short_options, long_options, &option_index);
        if (c == -1)    return CERR_OK;

        for (size_t i = 0; i < array_size(clap_cli_options_desc); i++) {
            auto desc = &clap_cli_options_desc[i];
            if (c != desc->short_name)  continue;

            if (desc->handle) {
                CERR_RET_CERR(desc->handle(ctx, desc->arg_required ? optarg : nullptr));
                goto next;
            }

            if (!desc->arg_required && desc->type != CLI_BOOL)  goto out_malformed;

            void *ptr = desc->ptr;
            if (!ptr)   ptr = (void *)ctx + desc->ctx_offset;

            switch (desc->type) {
                case CLI_BOOL:
                    *(bool *)ptr = desc->arg_required ? strtobool(optarg) : true;
                    goto next;

                case CLI_INT:
                    *(int *)ptr = atoi(optarg);
                    goto next;

                case CLI_LONG:
                    *(long *)ptr = strtol(optarg, NULL, 10);
                    goto next;

                case CLI_STR:
                    *(char **)ptr = optarg;
                    goto next;

                case CLI_NONE:
                default:
                    break;
            }

out_malformed:
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "malformed option '%s' / '%-1s'",
                .arg0   = desc->long_name ? : "<no-long-name>",
                .arg1   = &desc->short_name
            );
        }

        const char *opt = optind >= 0 ? ctx->argv[optind - 1] : "";
        if (ctx->cfg.cli_opt_cb)
            CERR_RET_CERR(ctx->cfg.cli_opt_cb(ctx, ctx->cfg.callback_data, &option_index));
        else
            return handle_help_opt(ctx, opt);
    }

    return CERR_OK;
}

DEFINE_CLEANUP(clap_context, if (*p) mem_free(*p))

cresp(clap_context) clap_init(struct clap_config *cfg, int argc, char **argv, char **envp)
{
    unsigned int log_flags = LOG_DEFAULT;
    LOCAL(clap_context, ctx);

    if (cfg && !clap_config_is_valid(cfg))
        return cresp_error(clap_context, CERR_INVALID_ARGUMENTS);

    ctx = mem_alloc(sizeof(*ctx), .zero = 1);
    if (!ctx)
        return cresp_error(clap_context, CERR_NOMEM);

    list_init(&ctx->luts);
    list_init(&ctx->timers);

    CERR_RET_T(clap_os_init(ctx), clap_context);

    mesh_init();

    if (cfg)
        memcpy(&ctx->cfg, cfg, sizeof(ctx->cfg));

    ctx->exit_after = -1;

    if (cfg->networking) {
        ctx->cfg.networking = memdup(cfg->networking, sizeof(*cfg->networking));
        ctx->cfg.networking->clap = ctx;
    }

    if (!ctx->cfg.cli_opts) ctx->cfg.cli_opts = CLAP_CLI_DEFAULT;

    ctx->argc = argc;
    ctx->argv = argv;
    ctx->envp = envp;

    CERR_RET_T(clap_cli_opts_process(ctx), clap_context);

    if (ctx->cfg.debug)
        log_flags = LOG_FULL;
    if (ctx->cfg.quiet)
        log_flags |= LOG_QUIET;

    CERR_RET_T(messagebus_init(ctx), clap_context);

    CERR_RET_T(subscribe(ctx, MT_COMMAND, clap_handle_command, ctx), clap_context);

#ifdef CONFIG_NETWORKING
    if (ctx->cfg.networking)
        CERR_RET(
            networking_init(ctx, ctx->cfg.networking, CLIENT),
            err_cerr(__cerr, "failed to initialize networking\n")
        );
#endif /* CONFIG_NETWORKING */

    if (ctx->cfg.early_init)
        CERR_RET_T(ctx->cfg.early_init(ctx, ctx->cfg.callback_data), clap_context);

    /* XXX: handle initialization errors */
    log_init(ctx, log_flags);
    (void)librarian_init(ctx->cfg.base_url);

    if (ctx->cfg.sound)
        /*
         * XXX: chicken an egg: start message sent, but ctx->sound is still NULL;
         * embed sound into ctx instead
         */
        ctx->sound = CRES_RET_T(sound_init(ctx), clap_context);
    if (ctx->cfg.phys)
        CHECK(ctx->phys = phys_init(ctx));

    int width = ctx->cfg.width, height = ctx->cfg.height;

    if (ctx->cfg.graphics) {
        clap_init_render_options(ctx);

        /*
         * XXX: it will get initialized in display_init(), but the pointer
         * is valid here. display_init() will call display_get_sizes(), which
         * will end up in clap_resize(), which needs a valid ctx->renderer.
         * But not to worry, the renderer will be initialized at that point.
         */
        CERR_RET_T(display_init(ctx, clap_frame, clap_resize), clap_context);

        display_get_sizes(&width, &height);

        if (ctx->cfg.font)
            ctx->font = CRES_RET_T(font_init(&ctx->renderer, ctx->cfg.default_font_name), clap_context);

        textures_init(&ctx->renderer);
        ctx->shaders = CRES_RET_T(shader_vars_init(&ctx->renderer), clap_context);

        lut_preset *lut_presets = ctx->cfg.lut_presets;
        if (!lut_presets)
            lut_presets = (lut_preset[]){ LUT_IDENTITY, LUT_MAX };
        CERR_RET_T(clap_lut_generate(ctx, lut_presets, 32), clap_context);

        CERR_RET_T(scene_init(&ctx->scene, ctx), clap_context);

        scene_camera_add(&ctx->scene);
        scene_cameras_calc(&ctx->scene);

        if (ctx->cfg.graphics_init)
            ctx->cfg.graphics_init(ctx, ctx->cfg.callback_data);

        CERR_RET_T(build_main_pl(ctx), clap_context);
    }
    if (ctx->cfg.input)
        (void)input_init(ctx); /* XXX: error handling */
    if (ctx->cfg.ui)
        CERR_RET_T(ui_init(&ctx->ui, ctx, width, height), clap_context);
    if (ctx->cfg.graphics && ctx->cfg.input)
        display_debug_ui_init(ctx);
    if (ctx->cfg.settings)
        CHECK(ctx->settings = settings_init(clap_settings_onload, ctx));

    return cresp_val(clap_context, NOCU(ctx));
}

void clap_done(struct clap_context *ctx, int status)
{
    if (ctx->cfg.ui)
        ui_done(&ctx->ui);
    if (ctx->cfg.graphics) {
        scene_done(&ctx->scene);
        ref_put(ctx->pl);
        shader_vars_done(ctx->shaders);
        luts_done(&ctx->luts);
        textures_done();
        display_done();
    }
    if (ctx->cfg.phys)
        phys_done(ctx->phys);
    if (ctx->cfg.sound)
        sound_done(ctx->sound);
    if (ctx->cfg.font)
        font_done(ctx->font);
    if (ctx->settings)
        settings_done(ctx->settings);
    if (ctx->cfg.networking)
        mem_free(ctx->cfg.networking);
    messagebus_done(ctx);
    free(ctx->os_info.name);
    clap_timers_done(ctx);
    mem_free(ctx);
    exit_cleanup_run(status);
}
