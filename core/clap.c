// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include "clap.h"
#include "common.h"
#include "input.h"
#include "lut.h"
#include "font.h"
#include "networking.h"
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
    shader_context      *shaders;
    struct list         luts;
    struct list         timers;
    struct ui           ui;
    int                 argc;
} clap_context;

/****************************************************************************
 * Context accessors
 ****************************************************************************/

struct clap_config *clap_get_config(struct clap_context *ctx)
{
    return &ctx->cfg;
}

renderer_t *clap_get_renderer(struct clap_context *ctx)
{
    return &ctx->renderer;
}

shader_context *clap_get_shaders(struct clap_context *ctx)
{
    return ctx->shaders;
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

clap_os_info *clap_ges_os(clap_context *ctx)
{
    return &ctx->os_info;
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
    f->fps_fine = f->ts_delta.tv_sec ? 1 : (NSEC_PER_SEC / f->ts_delta.tv_nsec);

    if (status) {
        memset(&m, 0, sizeof(m));
        m.type            = MT_COMMAND;
        m.cmd.status      = 1;
        m.cmd.fps         = f->fps_fine;
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
    if (!ctx || dt < 0.0 || !fn)
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
    if (!ctx || !timer || list_empty(&timer->entry)) {
        err("deleting nonexistent timer %p\n", timer);
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
        ctx->cfg.settings_cb(rs, ctx->cfg.settings_cb_data);
}

EMSCRIPTEN_KEEPALIVE void clap_frame(void *data)
{
    struct clap_context *ctx = data;
    struct ui *ui = clap_get_ui(ctx);

    mem_frame_begin();
    clap_fps_calc(ctx, &ctx->fps);
    clap_timers_run(ctx);

    int width, height;
    renderer_get_viewport(&ctx->renderer, NULL, NULL, &width, &height);

    imgui_render_begin(width, height);
    fuzzer_input_step();
    input_events_dispatch();

    PROF_FIRST(start);

    struct scene *scene = ctx->cfg.callback_data;
    if (scene->control) {
        /*
         * calls into character_move(): handle inputs, adjust velocities etc
         * for the physics step's dynamics simulation (dWorldStep())
         */
        scene_characters_move(scene);
    }

    PROF_STEP(move, start)

    double dt = ctx->fps.ts_delta.tv_nsec / (double)NSEC_PER_SEC;
    phys_step(clap_get_phys(ctx), dt);

    PROF_STEP(phys, move);

#ifndef CONFIG_FINAL
    networking_poll();
#endif

    PROF_STEP(net, phys);

    scene_update(scene);

    scene_cameras_calc(scene);

    ui_update(ui);

    PROF_STEP(updates, net);

    if (ctx->cfg.frame_cb)
        ctx->cfg.frame_cb(ctx->cfg.callback_data);

    PROF_STEP(callback, updates);

    models_render(ui->renderer, &ui->mq);

    PROF_STEP(ui_render, callback);

    profiler_show(PROF_PTR(start), ctx->fps.fps_fine);
    renderer_debug(&ctx->renderer);
    input_debug();

    imgui_render();
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
        &(struct message_input) {
            .resize = 1,
            .x      = width,
            .y      = height,
        },
        NULL
    );

    if (ctx->cfg.resize_cb)
        ctx->cfg.resize_cb(ctx->cfg.callback_data, width, height);

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

cerr clap_lut_generate(clap_context *ctx, lut_preset *presets, unsigned int side)
{
    if (!ctx->cfg.graphics)
        return CERR_INVALID_OPERATION;

    if (!presets)
        return CERR_INVALID_ARGUMENTS;

    for (int i = 0; presets[i] < LUT_MAX; i++) {
        CRES_RET(
            lut_generate(&ctx->luts, presets[i], side),
            { luts_done(&ctx->luts); return cerr_error_cres(__resp); }
        );
    }

    return CERR_OK;
}

static bool clap_config_is_valid(struct clap_config *cfg)
{
    if (cfg->graphics && (!cfg->frame_cb || !cfg->resize_cb || !cfg->title))
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

    if (ctx->cfg.debug)
        log_flags = LOG_FULL;
    if (ctx->cfg.quiet)
        log_flags |= LOG_QUIET;

    ctx->argc = argc;
    ctx->argv = argv;
    ctx->envp = envp;

    CERR_RET_T(messagebus_init(), clap_context);

    /* XXX: handle initialization errors */
    log_init(log_flags);
    (void)librarian_init(ctx->cfg.base_url);
    if (ctx->cfg.font)
        ctx->font = CRES_RET_T(font_init(ctx->cfg.default_font_name), clap_context);

    if (ctx->cfg.sound)
        CHECK(ctx->sound = sound_init());
    if (ctx->cfg.phys)
        CHECK(ctx->phys = phys_init());
    if (ctx->cfg.graphics) {
        /*
         * XXX: it will get initialized in display_init(), but the pointer
         * is valid here. display_init() will call display_get_sizes(), which
         * will end up in clap_resize(), which needs a valid ctx->renderer.
         * But not to worry, the renderer will be initialized at that point.
         */
        CERR_RET_T(display_init(ctx, clap_frame, clap_resize), clap_context);

        textures_init();
        ctx->shaders = CRES_RET_T(shader_vars_init(), clap_context);

        lut_preset *lut_presets = ctx->cfg.lut_presets;
        if (!lut_presets)
            lut_presets = (lut_preset[]){ LUT_IDENTITY, LUT_MAX };
        CERR_RET_T(clap_lut_generate(ctx, lut_presets, 32), clap_context);
    }
    if (ctx->cfg.input)
        (void)input_init(); /* XXX: error handling */
    if (ctx->cfg.ui)
        CERR_RET_T(ui_init(&ctx->ui, ctx, ctx->cfg.width, ctx->cfg.height), clap_context);
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
    if (ctx->cfg.sound)
        sound_done(ctx->sound);
    if (ctx->cfg.phys)
        phys_done(ctx->phys);
    if (ctx->cfg.graphics) {
        shader_vars_done(ctx->shaders);
        luts_done(&ctx->luts);
        textures_done();
        display_done();
    }
    if (ctx->cfg.font)
        font_done(ctx->font);
    if (ctx->settings)
        settings_done(ctx->settings);
    messagebus_done();
    free(ctx->os_info.name);
    clap_timers_done(ctx);
    mem_free(ctx);
    exit_cleanup_run(status);
}
