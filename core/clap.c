// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include <sys/utsname.h>
#endif
#include "clap.h"
#include "common.h"
#include "display.h"
#include "audio-settings.h"
#include "graphics-settings.h"
#include "input.h"
#include "input-controls.h"
#include "lut.h"
#include "font.h"
#include "fs-ops.h"
#include "networking.h"
#include "noise.h"
#include "pipeline-builder.h"
#include "platform.h"
#include "profiler.h"
#include "render.h"
#include "scene.h"
#include "sound.h"
#include "str.h"
#include "mesh.h"
#include "messagebus.h"
#include "librarian.h"
#include "physics.h"
#include "settings.h"
#include "ui.h"
#include "ui-debug.h"
#include "util.h"
#include "script.h"

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
    ;
}

const char *__asan_default_suppressions()
{
    return
#if defined(__APPLE__)
        /*
         * AppKit/CoreUI may parse system vector/PDF assets during Cocoa window layout.
         * ASAN's strndup interceptor reports CoreGraphics' bounded lexer overread.
         */
        "interceptor_via_fun:pdf_lexer_scan\n"
#elif !defined(CONFIG_BROWSER)
        "interceptor_via_lib:amdgpu_dri.so"
#endif /* !__APPLE__ && !CONFIG_BROWSER */
        "";
}

# ifdef __linux__
/**
 * on_wayland() - Tell if the process is going to talk to a Wayland compositor
 *
 * LSan asks for its suppressions before main(), so GLFW can't be asked which
 * platform it picked; mirror what wl_display_connect() does instead: an
 * inherited socket fd, or the socket named in the environment (absolute, or
 * relative to the runtime directory), or the default one in the runtime
 * directory. GLFW prefers Wayland over X11 whenever it can connect to it.
 *
 * Return: true if a Wayland connection is possible
 */
static bool on_wayland(void)
{
    if (getenv("WAYLAND_SOCKET"))       return true;

    const char *name = getenv("WAYLAND_DISPLAY") ?: "wayland-0";
    if (name[0] == '/')                 return !access(name, F_OK);

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir)                   return false;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", runtime_dir, name);

    return !access(path, F_OK);
}

# endif /* __linux__ */

/**
 * __lsan_default_suppressions() - Built-in LeakSanitizer suppressions
 *
 * GLFW's Wayland backend draws client-side decorations through libdecor,
 * whose cairo plugin fills process-wide pango and fontconfig caches that
 * nothing releases at glfwTerminate(); a couple of wl_proxy objects created
 * by a library that is unloaded again by then also survive. Suppress those,
 * but only when running on Wayland, so that leaks elsewhere stay visible.
 *
 * On vulkan builds, amdvlk's WSI drops xcb replies on the floor at teardown
 * (RADV, running the same engine code, exits leak-free); the engine never
 * talks to xcb directly, so this can't mask a leak of ours. amdvlk's other
 * leaks are internal to the driver and match the amdvlk64.so entry, since
 * render-vk.c keeps the ICDs mapped past the loader's dlclose() for
 * exactly this. Engine frames always resolve, so the engine's own leaks
 * still surface.
 *
 * Return: newline-separated leak suppressions, possibly empty
 */
const char *__lsan_default_suppressions(void)
{
# ifdef CONFIG_RENDERER_VULKAN
#  define LSAN_VULKAN_SUPPRESSIONS \
        "leak:xcb_wait_for_reply\n" \
        "leak:amdvlk64.so\n"
# else /* CONFIG_RENDERER_VULKAN */
#  define LSAN_VULKAN_SUPPRESSIONS
# endif /* !CONFIG_RENDERER_VULKAN */

# ifdef __linux__
    if (on_wayland())
        return
            "leak:libdecor-cairo\n"
            "leak:libfontconfig\n"
            "leak:libpango\n"
            "leak:libwayland-client\n"
            LSAN_VULKAN_SUPPRESSIONS;
# endif /* __linux__ */

    return LSAN_VULKAN_SUPPRESSIONS "";
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

typedef struct clap_fs_watch {
    struct list         entry;
    fs_watch            *watch;
    clap_fs_watch_cb    cb;
    void                *data;
    unsigned int        events;
    char                path[PATH_MAX];
} clap_fs_watch;

typedef struct clap_context {
    struct clap_config  cfg;
    struct fps_data     fps;
    char                **argv;
    char                **envp;
    clap_os_info        os_info;
    struct timespec     current_time;
    struct timespec     time_origin;
    sound_context       *sound;
    font_context        *font;
    struct phys         *phys;
    struct settings     *settings;
    prng                *prng;
    renderer_t          renderer;
    render_options      render_options;
    render_options      render_options_current;
    shader_context      *shaders;
    struct scene        scene;
    pipeline            *pl;
    struct list         luts;
    noise3d             *noise3d;
    struct list         timers;
    struct list         scripts;
    struct list         fs_watches;
    script_watch        *script_watch;
    const char          *script_dir;
    struct ui           ui;
    messagebus          mb;
    int                 argc;
    int                 exit_after;
    double              paused_time;
    cerr                pipeline_error;
    render_backend      renderer_want;
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

prng *clap_prng(struct clap_context *ctx)
{
    return ctx->prng;
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

render_backend clap_get_renderer_want(struct clap_context *ctx)
{
    return ctx->renderer_want;
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

noise3d *clap_get_noise3d(clap_context *ctx)    { return ctx->noise3d; }

struct timespec clap_get_current_timespec(struct clap_context *ctx)
{
    return ctx->current_time;
}

/**
 * clap_sample_time() - sample the clock into the context's engine time
 * @ctx:    clap context
 *
 * Engine time is relative to the first clock sample (the app epoch):
 * CLOCK_MONOTONIC is anchored at boot on Linux and at the unix epoch
 * on emscripten (performance.timeOrigin), magnitudes at which a
 * float32 cast loses whole seconds of precision -- shader uniforms
 * derived from engine time rely on it staying small.
 */
static void clap_sample_time(struct clap_context *ctx)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (unlikely(!ctx->time_origin.tv_sec && !ctx->time_origin.tv_nsec))
        ctx->time_origin = now;

    timespec_diff(&ctx->time_origin, &now, &ctx->current_time);
}

double clap_get_current_time(struct clap_context *ctx)
{
    /*
     * If clap_timer_set() is called before the first frame samples the
     * clock, it'll go off immediately. Prevent that.
     */
    if (unlikely(!ctx->time_origin.tv_nsec && !ctx->time_origin.tv_sec))
        clap_sample_time(ctx);

    return (double)ctx->current_time.tv_sec + (double)ctx->current_time.tv_nsec / NSEC_PER_SEC;
}

static void clap_fps_calc(struct clap_context *ctx, struct fps_data *f)
{
    bool status = false;
    struct message m;

    clap_sample_time(ctx);
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
    ctx->render_options.shadow_bleed = 0.9;
    ctx->render_options.shadow_msaa = false;
    ctx->render_options.laplace_kernel = 3;
    ctx->render_options.edges = true;
    ctx->render_options.render_scale = 1.0;
    ctx->render_options.edge_antialiasing = true;
    ctx->render_options.taa_feedback = 0.1;
    ctx->render_options.shadow_outline = true;
    ctx->render_options.shadow_outline_threshold = 0.4;
    ctx->render_options.shadow_caster_min_size = 24.0;
    ctx->render_options.hdr = true;
    ctx->render_options.hdr_output_enabled = true;
    /*
     * Apple Silicon's GL driver can't handle this much postprocessing
     * without driving FPS into single digits and heating up like a frying
     * pan. Disable it until Metal renderer is ready.
     */
#if !(defined(__APPLE__) && defined(CONFIG_RENDERER_OPENGL))
    ctx->render_options.ssao = true;
#endif /* __APPLE__ && __arm64__ */
    ctx->render_options.ao_method = AO_METHOD_GTAO;
    ctx->render_options.ssao_radius = 0.3;
    ctx->render_options.ssao_weight = 0.85;
    ctx->render_options.bloom_exposure = 1.7;
    ctx->render_options.bloom_intensity = 2.0;
    ctx->render_options.bloom_threshold = 0.27;
    ctx->render_options.bloom_operator = 1.0;
    /*
     * Halos are full size up to this distance and halve per doubling
     * beyond it, 0 disabling the scaling; the pyramid gives that law a
     * 10:1 range, the two blur chains 2:1 and a single chain none; the
     * player's bloom_quality picks between them, this value stays
     */
    ctx->render_options.bloom_distance = 3.0;
    ctx->render_options.bloom_quality = BLOOM_QUALITY_HIGH;
    ctx->render_options.lighting_exposure = 1.3;
    ctx->render_options.lighting_operator = 0.0;
    ctx->render_options.contrast = 0.15;
    ctx->render_options.fog = true;
    ctx->render_options.fog_near = 5.0;
    ctx->render_options.fog_far = 80.0;
    ctx->render_options.fog_max = 1.0;
    ctx->render_options.fog_curve = FOG_CURVE_LINEAR;
    vec3_dup(ctx->render_options.fog_color, (vec3){ 0.11, 0.14, 0.03 });
    ctx->render_options.volumetric_intensity = 1.0;
    ctx->render_options.volumetric_g = 0.6;
    ctx->render_options.volumetric_density = 0.15;
    ctx->render_options.volumetric_amp = 0.5;
    ctx->render_options.volumetric_scale = 0.05;
    ctx->render_options.volumetric_local_intensity = 1.0;
    ctx->render_options.volumetric_wind = 0.5;
    ctx->render_options.volumetric_falloff = 8.0;
    ctx->render_options.film_grain = true;
    ctx->render_options.film_grain_factor = 0.02;
    ctx->render_options.film_grain_power = 1.0;
    ctx->render_options.hdr_white_nits = 203.0f;
    ctx->render_options.hdr_peak_nits = 1000.0f;
    ctx->render_options.hdr_compress_knee = 2.0f;
    ctx->render_options.hdr_knee_softness = 4.0f;
    ctx->render_options.hdr_transfer_want = HDR_TRANSFER_PQ;

    /*
     * Platform overrides on top of the engine defaults; the game's own
     * graphics init runs later still and wins over both
     */
    const clap_platform *platform = platform_get();
    if (platform && platform->init_render_options)  platform->init_render_options(&ctx->render_options);
}

unsigned int clap_reserve_shadow_passes(clap_context *ctx, struct view *view, unsigned int nr)
{
    const clap_platform *platform = platform_get();
    unsigned int cap = CASCADES_MAX;

    if (platform && platform->shadow_passes_max)    cap = min(cap, platform->shadow_passes_max);

    view->nr_cascades = min(nr ? : cap, cap);

    return view->nr_cascades;
}

/* Per-frame render options update */
static void clap_update_render_options(clap_context *ctx)
{
    ctx->render_options.uptime = (float)clap_get_current_time(ctx);
    ctx->render_options.frame_count++;
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

cresp(clap_fs_watch) clap_fs_watch_add(clap_context *ctx, const char *path, unsigned int events,
                                       clap_fs_watch_cb cb, void *data)
{
#ifdef HAVE_FSWATCH
    if (!events)    return cresp_error(clap_fs_watch, CERR_INVALID_ARGUMENTS);

    fs_watch *watch = CRES_RET_T(fs_watch_open(&fs_watch_ops_native, path), clap_fs_watch);

    clap_fs_watch *w = mem_alloc(sizeof(*w), .zero = 1);
    if (!w) {
        CERR_RET(fs_watch_close(watch),);
        return cresp_error(clap_fs_watch, CERR_NOMEM);
    }

    w->watch = watch;
    w->cb = cb;
    w->data = data;
    w->events = events;
    snprintf(w->path, sizeof(w->path), "%s", path);
    list_append(&ctx->fs_watches, &w->entry);

    return cresp_val(clap_fs_watch, w);
#else
    return cresp_error(clap_fs_watch, CERR_NOT_SUPPORTED);
#endif /* HAVE_FSWATCH */
}

void clap_fs_watch_remove(clap_context *ctx, clap_fs_watch *w)
{
    list_del(&w->entry);
    CERR_RET(fs_watch_close(w->watch),);
    mem_free(w);
}

/* Poll every watch without blocking and dispatch what changed; runs each frame */
static void clap_fs_watches_run(clap_context *ctx)
{
    if (list_empty(&ctx->fs_watches))   return;

    clap_fs_watch *w, *iter;
    size_t n = 0;

    list_for_each_entry(w, &ctx->fs_watches, entry)
        n++;

    fs_watch *watches[n];
    size_t i = 0;

    list_for_each_entry(w, &ctx->fs_watches, entry)
        watches[i++] = w->watch;

    /* One non-blocking poll across every watch; nothing ready means nothing to drain */
    cerr pr = fs_watch_poll(watches, n, 0);
    if (IS_CERR(pr))                    return;

    list_for_each_entry_iter(w, iter, &ctx->fs_watches, entry)
        for (;;) {
            struct fs_watch_event ev;
            cerr err = fs_watch_read(w->watch, &ev);

            if (IS_CERR(err))           break;
            if (ev.flags & w->events)   w->cb(w->path, &ev, w->data);
        }
}

static void clap_fs_watches_done(clap_context *ctx)
{
    clap_fs_watch *w, *iter;

    list_for_each_entry_iter(w, iter, &ctx->fs_watches, entry)
        clap_fs_watch_remove(ctx, w);
}

static void clap_timers_pause(clap_context *ctx, bool pause)
{
    double dt;
    clap_timer *timer;

    if (pause) {
        ctx->paused_time = clap_get_current_time(ctx);
        return;
    }

    if (!ctx->paused_time)  return;

    dt = clap_get_current_time(ctx) - ctx->paused_time;
    ctx->paused_time = 0.0;

    if (dt <= 0.0)  return;

    list_for_each_entry(timer, &ctx->timers, entry)
        timer->time += dt;
}

static void clap_timers_run(clap_context *ctx)
{
    if (ctx->paused)    return;

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
 * Scripting
 ****************************************************************************/

#ifdef CONFIG_SCRIPTING
/* Wrapper node so struct script stays opaque to the context. */
struct clap_script_node {
    struct list     entry;
    struct script   *script;
};

cerr clap_script_add(clap_context *ctx, struct script *s)
{
    struct clap_script_node *node = mem_alloc(sizeof(*node), .zero = 1);
    if (!node)  return CERR_NOMEM;

    node->script = ref_get(s);
    list_append(&ctx->scripts, &node->entry);

    return CERR_OK;
}

void clap_script_remove(clap_context *ctx, struct script *s)
{
    struct clap_script_node *node;

    list_for_each_entry(node, &ctx->scripts, entry)
        if (node->script == s) {
            list_del(&node->entry);
            ref_put(node->script);
            mem_free(node);
            return;
        }
}

static void clap_scripts_run_frame(clap_context *ctx)
{
    struct clap_script_node *node;

    list_for_each_entry(node, &ctx->scripts, entry)
        script_run_frame(node->script);
}

static void clap_scripts_done(clap_context *ctx)
{
    struct clap_script_node *node, *iter;

    list_for_each_entry_iter(node, iter, &ctx->scripts, entry) {
        list_del(&node->entry);
        ref_put(node->script);
        mem_free(node);
    }
}
#else /* !CONFIG_SCRIPTING */
static inline void clap_scripts_run_frame(clap_context *ctx) {}
static inline void clap_scripts_done(clap_context *ctx) {}
#endif /* !CONFIG_SCRIPTING */

/****************************************************************************
 * Default fs watches
 ****************************************************************************/

/**
 * clap_default_fs_watches_init() - start the engine's own directory watches
 * @ctx:    clap_context
 *
 * Currently that is the hot-reloaded scripts directory: base_url/scripts
 * unless --scripts says otherwise. Nothing to watch where the platform has
 * no watch backend, and a config opts out with
 * clap_config::no_default_fs_watches.
 */
static void clap_default_fs_watches_init(clap_context *ctx)
{
    if (ctx->cfg.no_default_fs_watches)         return;

    char dir[PATH_MAX];
    const char *script_dir = ctx->script_dir;

    if (!script_dir) {
        CERR_RET(path_join(dir, sizeof(dir), ctx->cfg.base_url ? : "", "scripts"),
                 { err_cerr(__cerr, "scripts directory path too long\n"); return; });
        script_dir = dir;
    }

    cresp(script_watch) res = script_watch_dir(ctx, script_dir);
    /* No watch backend on this platform: nothing to keep in sync with */
    if (IS_CERR_CODE(res, CERR_NOT_SUPPORTED))  return;

    ctx->script_watch = CRES_RET(res, { err_cerr(res, "cannot watch scripts in '%s'\n", script_dir); return; });
}

/**
 * clap_default_fs_watches_done() - stop the engine's own directory watches
 * @ctx:    clap_context
 */
static void clap_default_fs_watches_done(clap_context *ctx)
{
    script_watch_done(ctx->script_watch);
}

/****************************************************************************
 * Rendering pipeline
 ****************************************************************************/

#define REBUILD_REASON_FIELD(_field) \
    { \
        .name   = __stringify(_field), \
        .offset = offsetof(render_options, _field), \
        .size   = field_sizeof(render_options, _field) \
    }

/* Table of boolean settings that if changed require a pipeline rebuild */
static const struct rebuild_reason {
    const char  *name;
    size_t      offset;
    size_t      size;
} rebuild_reason[] = {
    REBUILD_REASON_FIELD(hdr),
    REBUILD_REASON_FIELD(render_scale),
    REBUILD_REASON_FIELD(shadow_msaa),
    REBUILD_REASON_FIELD(model_msaa),
    REBUILD_REASON_FIELD(edges),
    REBUILD_REASON_FIELD(edge_sobel),
    REBUILD_REASON_FIELD(ssao),
    REBUILD_REASON_FIELD(ao_method),
    REBUILD_REASON_FIELD(shadow_vsm),
    REBUILD_REASON_FIELD(edge_antialiasing),
    REBUILD_REASON_FIELD(taa),
    REBUILD_REASON_FIELD(fog_volumetric),
    REBUILD_REASON_FIELD(lighting_lut),
    REBUILD_REASON_FIELD(bloom_quality),
    REBUILD_REASON_FIELD(light_heatmap),
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
                .noise3d        = noise3d_texture(clap_get_noise3d(ctx)),
                .nr_cascades    = scene->cameras[0].view.nr_cascades,
                .name           = "main"
            },
            .mq         = &scene->mq,
            .pl         = ctx->pl
        })
    );

    /* re-resolve variant-managed model shaders against the new options */
    CERR_RET_CERR(pipeline_shaders_resolve(ctx->pl, &scene->mq));

    clap_sync_render_options(ctx, false);

    return CERR_OK;
}

static cerr rebuild_pl_if_needed(clap_context *ctx)
{
    /* Nothing to rebuild: this configuration builds no graphics pipeline */
    if (!ctx->pl)   return CERR_OK;

    render_options *ropts = &ctx->render_options;
    render_options *ropts_current = &ctx->render_options_current;

    bool rebuild_pl = false;
    for (size_t i = 0; i < array_size(rebuild_reason); i++) {
        void *a = (void *)((void *)ropts + rebuild_reason[i].offset);
        void *b = (void *)((void *)ropts_current + rebuild_reason[i].offset);
        if (memcmp(a, b, rebuild_reason[i].size)) {
            rebuild_pl = true;
            dbg("pipeline rebuild reason: %s\n", rebuild_reason[i].name);
        }
    }

    if (rebuild_pl) {
        pipeline_clearout(ctx->pl);
        return build_main_pl(ctx);
    }

    return CERR_OK;
}

void clap_sync_render_options(clap_context *ctx, bool may_rebuild)
{
    /* No pipeline: the sync will happen at the end of build_main_pl() */
    if (!ctx->pl)   return;

    if (!may_rebuild) {
        memcpy(&ctx->render_options_current, &ctx->render_options, sizeof(render_options));
        return;
    }

    rebuild_pl_if_needed(ctx);
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
    CERR_RET(input_controls_init(rs), err_cerr(__cerr, "failed to load input controls\n"));
    clap_update_input_controls(ctx);
    CERR_RET(graphics_settings_init(ctx, rs), err_cerr(__cerr, "failed to load graphics settings\n"));
    CERR_RET(audio_settings_init(ctx, rs), err_cerr(__cerr, "failed to load audio settings\n"));

    if (ctx->cfg.settings_cb)
        ctx->cfg.settings_cb(ctx, rs, ctx->cfg.settings_cb_data);
}

EMSCRIPTEN_KEEPALIVE void clap_frame(void *data)
{
    struct clap_context *ctx = data;
    struct ui *ui = clap_get_ui(ctx);

    mem_frame_begin();
    clap_fps_calc(ctx, &ctx->fps);

    clap_update_render_options(ctx);

    clap_timers_run(ctx);
    clap_fs_watches_run(ctx);

    if (ctx->fullscreen) {
        ctx->fullscreen = false;
        display_enter_fullscreen();
    }

    /* no HDR output on GL even if display_supports_edr() == true */
    if (renderer_get_caps(&ctx->renderer)->renderer == RENDER_OPENGL)
        ctx->render_options.hdr_output_enabled = false;

    ctx->render_options.hdr_output = ctx->render_options.hdr_output_enabled
        ? display_supports_edr()
        : false;

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

    clap_scripts_run_frame(ctx);

    PROF_STEP(callback, updates);

    /*
     * Read back after the updates rather than at the top of the frame: the
     * debug UI switches the HDR transfer from scene_update() and the surface
     * is recreated on the spot, so the encode present.frag picks has to
     * follow the surface this frame has, not the one last frame had
     */
    ctx->render_options.hdr_transfer = renderer_hdr_transfer(&ctx->renderer);

    if (ctx->pl && !IS_CERR(ctx->pipeline_error)) {
        unsigned int ui_checkpoint = ui_state_get(ui) < UI_ST_RUNNING ? 0 : 1;
        cerr err = pipeline_render(ctx->pl, clap_is_paused(ctx) ? ui_checkpoint : 0);
        if (IS_CERR(err)) {
            err_cerr(err, "pipeline failed to render\n");
            ctx->pipeline_error = err;
        }
    } else {
        renderer_swapchain_begin(&ctx->renderer, FBOLOAD_CLEAR);
    }

    /* materials that changed since their models' variants were resolved */
    if (ctx->pl && pipeline_shaders_are_stale(ctx->pl))
        CERR_RET(pipeline_shaders_resolve(ctx->pl, &scene->mq),
                 err_cerr(__cerr, "failed to resolve shader variants\n"));

    rebuild_pl_if_needed(ctx);

    PROF_STEP(scene_render, callback);

    // In case of rendering failure, report the error and destroy
    // the ui object, which for this purpose means ui->mq becomes
    // an empty list and subsequent calls to models_render() on it
    // are effectively NOPs
    CERR_RET(
        models_render(&ctx->renderer, &ui->mq),
        { ui_done(ui); err_cerr(__cerr, "UI rendering failed\n"); }
    );

    PROF_STEP(ui_render, scene_render);

    profiler_show(PROF_PTR(start), ctx->fps.fps_fine);
    renderer_debug(&ctx->renderer);
    pipeline_debug(ctx->pl);
    input_debug();
    controllers_debug();
    memory_debug();
    script_repl_show(ctx);
    script_watch_show(ctx->script_watch);

    imgui_render();
    renderer_swapchain_end(&ctx->renderer);
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

static void os_info_dbg(clap_os_info *osi)
{
    dbg("os %d v%u.%u.%u%s browser %d v%u.%u.%u webkit v%u.%u.%u\n",
        osi->os, osi->os_version.major, osi->os_version.minor, osi->os_version.patch,
        osi->mobile ? " (mobile)" : "",
        osi->browser, osi->browser_version.major, osi->browser_version.minor,
        osi->browser_version.patch,
        osi->webkit_version.major, osi->webkit_version.minor, osi->webkit_version.patch);
}

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
    dbg("### user-agent: '%s'\n", ctx->os_info.name);

    if (!ctx->os_info.name)
        return CERR_NOMEM;

    clap_os_parse_user_agent(&ctx->os_info, ctx->os_info.name);
    os_info_dbg(&ctx->os_info);

    return CERR_OK;
}
#elif defined(_WIN32)
static cerr clap_os_init(struct clap_context *ctx)
{
    ctx->os_info.name = strdup("Windows");
    ctx->os_info.os = CLAP_OS_WINDOWS;
    os_info_dbg(&ctx->os_info);

    return ctx->os_info.name ? CERR_OK : CERR_NOMEM;
}
#else
static cerr clap_os_init(struct clap_context *ctx)
{
    struct utsname u;

    if (uname(&u) < 0) {
        ctx->os_info.name = strdup("unknown");
        return ctx->os_info.name ? CERR_OK : CERR_NOMEM;
    }

    char buf[sizeof(u.sysname) + sizeof(u.release) + 1];
    snprintf(buf, sizeof(buf), "%s %s", u.sysname, u.release);
    ctx->os_info.name = strdup(buf);
    if (!ctx->os_info.name)  return CERR_NOMEM;

    if (!strcmp(u.sysname, "Linux"))        ctx->os_info.os = CLAP_OS_LINUX;
    else if (!strcmp(u.sysname, "Darwin"))  ctx->os_info.os = CLAP_OS_MACOS;
    clap_semver_parse(u.release, &ctx->os_info.os_version);
    os_info_dbg(&ctx->os_info);

    return CERR_OK;
}
#endif /* __EMSCRIPTEN__ */

/****************************************************************************
 * Main API
 ****************************************************************************/

void clap_update_input_controls(struct clap_context *ctx)
{
    if (!ctx->cfg.graphics || !ctx->cfg.input)  return;

    bool want = input_controls_use_mouse() && !ctx->paused;
    display_mouse_capture(want);
}

static int clap_handle_command(struct clap_context *ctx, struct message *m, void *data)
{
    if (m->type != MT_COMMAND)  return MSG_HANDLED;

    if (m->cmd.toggle_modality) {
        ctx->paused = !ctx->paused;
        clap_timers_pause(ctx, ctx->paused);
        clap_update_input_controls(ctx);
    }

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

/* -R/--renderer: the backend to force, looked up against the backend table */
static cerr handle_renderer_opt(clap_context *ctx, const char *optarg)
{
    ctx->renderer_want = CRES_RET(renderer_backend_by_name(optarg), {
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "unknown rendering backend '%s'",
            .arg0   = optarg
        );
    });

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
    int             short_name;
    cli_opt_type    type;
    size_t          ctx_offset;
    void            *ptr;
    cerr            (*handle)(clap_context *ctx, const char *optarg);
    bool            arg_required;
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
    [CLAP_CLI_SCRIPT_DIR_BIT] = {
        .long_name      = "scripts",
        .short_name     = 's',
        .help           = "directory of Umka scripts to watch and hot-reload (default: <base_url>/scripts)",
        .arg_help       = "dir",
        .arg_required   = true,
        .type           = CLI_STR,
        .ctx_offset     = offsetof(clap_context, script_dir)
    },
    [CLAP_CLI_RENDERER_BIT] = {
        .long_name      = "renderer",
        .short_name     = 'R',
        .help           = "rendering backend to use (OpenGL, Vulkan, Metal); default: the first one that works",
        .arg_help       = "name",
        .arg_required   = true,
        .type           = CLI_STR,
        .handle         = handle_renderer_opt
    },
};

/*
 * The value getopt_long() returns for option @i: its short name, or a
 * number above the character range for options that have none
 */
static int cli_opt_val(size_t i)
{
    auto desc = &clap_cli_options_desc[i];

    return desc->short_name ? desc->short_name : (int)(256 + i);
}

#define HELP_OPT_PFX "  "
static cerr handle_help_opt(clap_context *ctx, const char *optarg)
{
    if (optarg) err("Unrecognized option '%s'\n", optarg);

    msg("Usage: %s [OPTIONS]\n\nOptions:\n", str_basename(ctx->argv[0]));
    for (size_t i = 0; i < array_size(clap_cli_options_desc); i++) {
        if (!(ctx->cfg.cli_opts & (1u << i)))   continue;

        auto desc = &clap_cli_options_desc[i];

        char help[256] = {};
        declare_sv(help);

        if (desc->short_name)
            sv_append(&sv(help), "%s-%c", HELP_OPT_PFX, desc->short_name);
        if (desc->arg_help)
            sv_append(&sv(help), " <%s>", desc->arg_help);
        if (desc->long_name)
            sv_append(&sv(help), "%s--%s", sv_len(&sv(help)) ? ", " : HELP_OPT_PFX, desc->long_name);
        if (desc->arg_help)
            sv_append(&sv(help), " <%s>", desc->arg_help);
        if (desc->help)
            sv_append(&sv(help), "\n%s%s%s", HELP_OPT_PFX, HELP_OPT_PFX, desc->help);

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
        int val = cli_opt_val(i);
        long_options[lopts_idx++] = (struct option) {
            desc->long_name,
            desc->arg_required ? required_argument : no_argument,
            NULL,
            val
        };

        if (desc->short_name) {
            short_options[sopts_idx++] = desc->short_name;
            if (desc->arg_required) short_options[sopts_idx++] = ':';
        }
    }

    long_options[lopts_idx] = (struct option) {};
    short_options[sopts_idx] = 0;

    int option_index;

    opterr = 0;
    for (;;) {
next:
        int c = getopt_long(ctx->argc, ctx->argv, short_options, long_options, &option_index);
        if (c == -1)    return CERR_OK;
        if (c == '?')   goto handle_or_bail;

        for (size_t i = 0; i < array_size(clap_cli_options_desc); i++) {
            auto desc = &clap_cli_options_desc[i];
            if (c != cli_opt_val(i))    continue;

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

handle_or_bail:
        const char *opt = nullptr;
        if (c == '?' && option_index >= 0 && option_index < ctx->argc)  opt = ctx->argv[option_index];
        if (!opt)   opt = optind >= 0 ? ctx->argv[optind - 1] : "";

        if (ctx->cfg.cli_opt_cb)
            CERR_RET_CERR(ctx->cfg.cli_opt_cb(ctx, ctx->cfg.callback_data, &option_index));
        else
            return handle_help_opt(ctx, opt);
    }

    return CERR_OK;
}

DEFINE_CLEANUP(clap_context, if (*p) { prng_free((*p)->prng); mem_free(*p); })

cresp(clap_context) clap_init(struct clap_config *cfg, int argc, char **argv, char **envp)
{
    unsigned int log_flags = LOG_DEFAULT;
    LOCAL(clap_context, ctx);

    if (cfg && !clap_config_is_valid(cfg))
        return cresp_error(clap_context, CERR_INVALID_ARGUMENTS);

    platform_init();

    ctx = mem_alloc(sizeof(*ctx), .zero = 1);
    if (!ctx)
        return cresp_error(clap_context, CERR_NOMEM);

    list_init(&ctx->luts);
    list_init(&ctx->timers);
    list_init(&ctx->scripts);
    list_init(&ctx->fs_watches);

    CERR_RET_T(clap_os_init(ctx), clap_context);

    mesh_init();

    if (cfg)
        memcpy(&ctx->cfg, cfg, sizeof(ctx->cfg));

    ctx->exit_after = -1;
    ctx->renderer_want = RENDER_MAX;

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
            networking_init(ctx, ctx->cfg.networking, NET_CLIENT),
            err_cerr(__cerr, "failed to initialize networking\n")
        );
#endif /* CONFIG_NETWORKING */

    /* before early_init(), so the application can split streams off it */
    ctx->prng = CRES_RET_T(prng_new(ctx->cfg.prng_alg, ctx->cfg.prng_seed), clap_context);

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

        CERR_RET_T(display_init(ctx, clap_frame, clap_resize), clap_context);

        /*
         * Prime the viewport for scene/pipeline construction below; the
         * initial resize is delivered at the end of clap_init(), when all
         * of its consumers are in place.
         */
        display_get_sizes(&width, &height);
        renderer_viewport(&ctx->renderer, 0, 0, width, height);

        if (ctx->cfg.font)
            ctx->font = CRES_RET_T(font_init(ctx->cfg.default_font_name), clap_context);

        textures_init(&ctx->renderer);
        ctx->shaders = CRES_RET_T(shader_vars_init(&ctx->renderer), clap_context);

        lut_preset *lut_presets = ctx->cfg.lut_presets;
        if (!lut_presets)
            lut_presets = (lut_preset[]){ LUT_IDENTITY, LUT_MAX };
        CERR_RET_T(clap_lut_generate(ctx, lut_presets, 64), clap_context);

        ctx->noise3d = CRES_RET_T(ref_new_checked(noise3d, .renderer = &ctx->renderer), clap_context);

        CERR_RET_T(scene_init(&ctx->scene, ctx), clap_context);

        scene_camera_add(&ctx->scene);
        scene_cameras_calc(&ctx->scene);

        if (ctx->cfg.graphics_init)
            ctx->cfg.graphics_init(ctx, ctx->cfg.callback_data);

        /*
         * The default transfer is a wish; where the surface can't grant it,
         * start from one it can instead of quietly landing on a fallback
         */
        unsigned int transfers = renderer_hdr_transfers(&ctx->renderer);
        if (transfers && !(transfers & (1u << ctx->render_options.hdr_transfer_want)))
            ctx->render_options.hdr_transfer_want = __builtin_ffs(transfers) - 1;

        renderer_hdr_transfer_set(&ctx->renderer, ctx->render_options.hdr_transfer_want);
        renderer_hdr_enable(&ctx->renderer, ctx->render_options.hdr_output_enabled);

        CERR_RET_T(build_main_pl(ctx), clap_context);
    }
    if (ctx->cfg.input)
        (void)input_init(ctx); /* XXX: error handling */
    if (ctx->cfg.ui) {
        CERR_RET_T(ui_init(&ctx->ui, ctx, width, height), clap_context);
        CERR_RET_T(ui_menus_init(&ctx->ui, &ctx->cfg.ui_menu, &ctx->cfg.ui_start_menu),
                   clap_context);
    }
    if (ctx->cfg.graphics && ctx->cfg.input)
        display_debug_ui_init(ctx);
    if (ctx->cfg.settings)
        CHECK(ctx->settings = settings_init(clap_settings_onload, ctx));

    /*
     * Deliver the initial window geometry through the same path that
     * window system resizes take. GLFW backends also get a real resize
     * event when the window maps, but browsers never send one.
     */
    if (ctx->cfg.graphics)
        display_resize(width, height);

    clap_default_fs_watches_init(ctx);

    return cresp_val(clap_context, NOCU(ctx));
}

void clap_done(struct clap_context *ctx, int status)
{
    /* Drop the script watch and the scripts first, while the ui their widgets live in is up */
    clap_default_fs_watches_done(ctx);
    clap_scripts_done(ctx);
    if (ctx->cfg.ui) {
        ui_menus_done(&ctx->ui);
        ui_done(&ctx->ui);
    }
    if (ctx->cfg.graphics) {
        scene_done(&ctx->scene);
        ref_put(ctx->pl);
        shader_vars_done(ctx->shaders);
        luts_done(&ctx->luts);
        ref_put_last(ctx->noise3d);
        pipeline_builder_done();
        textures_done();
        display_done();
    }
    if (ctx->cfg.phys)
        phys_done(ctx->phys);
    if (ctx->cfg.sound)
        sound_done(ctx->sound);
    if (ctx->cfg.font)
        font_done(ctx->font);
    input_controls_done();
    if (ctx->settings)
        settings_done(ctx->settings);
    if (ctx->cfg.networking) {
#ifdef CONFIG_NETWORKING
        networking_done();
#endif /* CONFIG_NETWORKING */
        mem_free(ctx->cfg.networking);
    }
    messagebus_done(ctx);
    free(ctx->os_info.name);
    clap_timers_done(ctx);
    prng_free(ctx->prng);
    clap_fs_watches_done(ctx);
    mem_free(ctx);
    exit_cleanup_run(status);
}
