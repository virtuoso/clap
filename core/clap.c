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

typedef struct clap_context {
    struct clap_config  cfg;
    struct fps_data     fps;
    char                **argv;
    char                **envp;
    struct timespec     current_time;
    sound_context       *sound;
    font_context        *font;
    struct phys         *phys;
    struct settings     *settings;
    renderer_t          renderer;
    shader_context      *shaders;
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

    int width, height;
    renderer_get_viewport(&ctx->renderer, NULL, NULL, &width, &height);

    imgui_render_begin(width, height);
    fuzzer_input_step();

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

    unsigned long frame_count = max((unsigned long)display_refresh_rate() / clap_get_fps_fine(ctx), 1);

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

    profiler_show(PROF_PTR(start));

    imgui_render();
    display_swap_buffers();

    scene->frames_total += frame_count;
    ui->frames_total    += frame_count;
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

    struct scene *scene = ctx->cfg.callback_data;
    scene->width = width;
    scene->height = height;
    scene->proj_update++;

    if (ctx->cfg.resize_cb)
        ctx->cfg.resize_cb(ctx->cfg.callback_data, width, height);

    touch_input_set_size(width, height);
}

static bool clap_config_is_valid(struct clap_config *cfg)
{
    if (cfg->graphics && (!cfg->frame_cb || !cfg->resize_cb || !cfg->title))
        return false;
    if (cfg->ui && !cfg->graphics)
        return false;

    return true;
}

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

    cerr err = messagebus_init();
    if (IS_CERR(err))
        return cresp_error(clap_context, CERR_NOMEM);

    /* XXX: handle initialization errors */
    log_init(log_flags);
    (void)librarian_init(ctx->cfg.base_url);
    if (ctx->cfg.font) {
        cresp(font_context) res = font_init(ctx->cfg.default_font_name);
        if (IS_CERR(res))
            return cresp_error_cerr(clap_context, res);

        ctx->font = res.val;
    }

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
        cerr err = display_init(ctx, clap_frame, clap_resize);
        if (IS_CERR(err))
            return cresp_error_cerr(clap_context, err);

        textures_init();
        cresp(shader_context) res = shader_vars_init();
        if (IS_CERR(res))
            return cresp_error_cerr(clap_context, res);

        ctx->shaders = res.val;
    }
    if (ctx->cfg.input)
        (void)input_init(); /* XXX: error handling */
    if (ctx->cfg.ui) {
        cerr err = ui_init(&ctx->ui, ctx, ctx->cfg.width, ctx->cfg.height);
        if (IS_CERR(err))
            return cresp_error_cerr(clap_context, err);
    }
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
        textures_done();
        display_done();
    }
    if (ctx->cfg.font)
        font_done(ctx->font);
    if (ctx->settings)
        settings_done(ctx->settings);
    messagebus_done();
    mem_free(ctx);
    exit_cleanup_run(status);
}
