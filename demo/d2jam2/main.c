// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "clap.h"
#include "display.h"
#include "font.h"
#include "input.h"
#include "loading-screen.h"
#include "lut.h"
#include "messagebus.h"
#include "networking.h"
#include "scene.h"
#include "settings.h"
#include "sound.h"
#include "ui.h"
#include "ui-debug.h"
#include "d2jam2.h"

static sound *title_sound;
static bool title_armed;
/* the cave fills up once both it and the settings with the crystals' seed are in */
static bool cave_built;
static bool settings_loaded;
static bool cave_filled;

static const char *intro_osd[] = {
    "WASD to move the character",
    "Space to jump / Shift to dash",
    "Arrows to move the camera",
};

static const char *title_osd[] = {
    "GATHERING",
    "SKILLS",
    "IN A CAVE"
};

/**
 * title_osd_element_cb() - schedule a line of the title OSD
 * @uie:    the line's element
 * @i:      the line's index
 */
static void title_osd_element_cb(struct ui_element *uie, unsigned int i)
{
    uia_skip_duration(uie, i * 4.0);
    uia_set_visible(uie, 1);
    uia_skip_duration(uie, 3.0);
    uia_lin_float(uie, ui_element_set_alpha, 1.0, 0.0, true, 0.5);
    uia_set_visible(uie, 0);
}

/**
 * title_show() - timer callback: roll the title and play its sound
 * @data:   the scene
 */
static void title_show(void *data)
{
    struct scene *scene = data;
    struct ui *ui = clap_get_ui(scene->clap_ctx);
    struct font *font = ref_new(font,
                                .ctx    = clap_get_font(scene->clap_ctx),
                                .name   = "ofl/ZillaSlab-Bold.ttf",
                                .size   = 240);

    if (title_sound)    sound_play(title_sound);

    /* the OSD takes over the font reference; NULL gets it the default font */
    ui_osd_new(
        ui,
        &(const struct ui_widget_builder) {
            .el_affinity    = UI_AF_CENTER | UI_SZ_WIDTH_FRAC,
            .affinity       = UI_AF_CENTER | UI_SZ_FRAC,
            .el_w           = 0.9,
            .el_h           = 100,
            .el_margin      = 4,
            .w              = 0.8,
            .h              = 0.9,
            .font           = font,
            .el_cb          = title_osd_element_cb,
            .el_color       = { 0.0, 0.0, 0.0, 0.0 },
            .text_color     = { 0.8, 0.8, 0.8, 1.0 },
        },
        title_osd,
        array_size(title_osd)
    );
}

/**
 * title_kickoff() - roll the title ten seconds after the player first moves
 * @scene:  the scene
 */
static void title_kickoff(struct scene *scene)
{
    if (title_armed)    return;

    title_armed = true;
    CRES_RET(clap_timer_set(scene->clap_ctx, 10.0, NULL, title_show, scene),
             err_cerr(__resp, "no title for you\n"));
}

/**
 * enum main_state - the demo's progress
 * @MS_STARTING:    before the first frame
 * @MS_RUNNING:     the intro OSD is up
 */
enum main_state {
    MS_STARTING = 0,
    MS_RUNNING,
};

static enum main_state main_state;

/**
 * render_frame() - frame callback: show the intro OSD on the first frame
 * @ctx:    clap context
 * @data:   callback data
 */
static EMSCRIPTEN_KEEPALIVE void render_frame(clap_context *ctx, void *data)
{
    struct ui *ui = clap_get_ui(ctx);

    if (main_state == MS_STARTING) {
        main_state++;
        ui_osd_new(ui, NULL, intro_osd, array_size(intro_osd));
    }
}

/**
 * handle_input() - input handler: the first move kicks the title off
 * @ctx:    clap context
 * @m:      input message
 * @data:   the scene
 * Return: 0
 */
static int handle_input(struct clap_context *ctx, struct message *m, void *data)
{
    struct scene *scene = data;

    if (fabsf(m->input.delta_lx) > 1e-3 || m->input.up || m->input.down || m->input.left || m->input.right)
        title_kickoff(scene);

    return 0;
}

/**
 * cave_seed() - the crystals' layout seed
 * @settings:   settings, loaded
 *
 * Stored in the settings on the first run, so the crystals keep their
 * places across runs of an installation.
 * Return: the seed
 */
static uint64_t cave_seed(struct settings *settings)
{
    JsonNode *group = settings_find_get(settings, NULL, "d2jam2", JSON_OBJECT);
    uint64_t seed = group ? settings_get_num(settings, group, "rng_seed") : 0;

    if (!seed) {
        /* the frame clock counts from the start of the run: ask the wall clock */
        struct timespec ts;

        clock_gettime(CLOCK_REALTIME, &ts);
        seed = ts.tv_nsec ^ ts.tv_sec;
        if (group)  settings_set_num(settings, group, "rng_seed", (double)seed);
    }

    return seed;
}

/**
 * cave_fill() - fill the cave once both it and the settings are in
 * @ctx:    clap context
 *
 * Natively the settings load in clap_init(), before the cave is built;
 * in a browser they arrive from storage after main() has returned, and
 * the cave fills up then.
 */
static void cave_fill(clap_context *ctx)
{
    if (!cave_built || !settings_loaded || cave_filled) return;

    cave_filled = true;
    CERR_RET(
        noisy_mesh(clap_get_scene(ctx), cave_seed(clap_get_settings(ctx))),
        err_cerr(__cerr, "failed to populate the cave\n")
    );
}

/**
 * settings_loaded_cb() - settings callback: the crystals' seed is in
 * @ctx:        clap context
 * @settings:   the settings
 * @data:       callback data
 */
static void settings_loaded_cb(clap_context *ctx, struct settings *settings, void *data)
{
    settings_loaded = true;
    cave_fill(ctx);
}

/**
 * graphics_init() - graphics callback: the camera, the render options and the LUT
 * @ctx:    clap context
 * @data:   callback data
 */
static void graphics_init(clap_context *ctx, void *data)
{
    struct scene *scene = clap_get_scene(ctx);
    render_options *ropts = clap_get_render_options(ctx);

    /*
     * The only shadow caster is the character's headlamp, a spotlight,
     * which needs a single cascade
     */
    scene->camera->view.main.far_plane = 700.0;
    clap_reserve_shadow_passes(ctx, &scene->camera->view, 1);

    ropts->fog_near = 10.0;
    ropts->fog_far = 200.0;
    vec3_dup(ropts->fog_color, (vec3) { 0.0, 0.35, 0.605 });
    ropts->lighting_operator = 1.0;
    ropts->contrast = 0.7;
    ropts->lighting_exposure = 2.6;
    ropts->bloom_threshold = 0.7;
    ropts->bloom_exposure = 2.6;
    ropts->bloom_intensity = 2.4;
    ropts->film_grain = true;

    CERR_RET(
        clap_set_lighting_lut(ctx, "deep sea abyss"),
        err_cerr(__cerr, "failed to set LUT\n")
    );
}

int main(int argc, char **argv, char **envp)
{
    struct clap_config cfg = {
        .debug          = 1,
        .input          = 1,
        .font           = 1,
        .sound          = 1,
        .phys           = 1,
        .graphics       = 1,
        .ui             = 1,
        .ui_menu        = { .enable = true },
        .settings       = 1,
        .title          = CLAP_EXECUTABLE_TITLE,
#ifndef CONFIG_BROWSER
        .base_url       = "demo/d2jam2/",
#endif /* !CONFIG_BROWSER */
        .width          = 1280,
        .height         = 720,
#ifndef CONFIG_FINAL
        .networking     = &(struct networking_config) {
            .server_ip     = CONFIG_SERVER_IP,
            .server_port   = CONFIG_SERVER_PORT,
            .server_wsport = CONFIG_SERVER_WSPORT,
            .logger        = 1,
        },
#endif /* !CONFIG_FINAL */
        .graphics_init  = graphics_init,
        .settings_cb    = settings_loaded_cb,
        .frame_cb       = render_frame,
        .default_font_name  = CLAP_FONT_FILE,
#ifdef CONFIG_FINAL
        .lut_presets    = (lut_preset[]){ LUT_SCIFI_BLUEGREEN, LUT_DEEP_SEA_ABYSS, LUT_BLOODVEIL_CRIMSON, LUT_MAX },
#else
        .lut_presets    = lut_presets_all,
#endif /* !CONFIG_FINAL */
    };

    cresp(clap_context) clap_res = clap_init(&cfg, argc, argv, envp);
    if (IS_CERR(clap_res)) {
        err_cerr(clap_res, "failed to initialize clap\n");
        return CERR_TO_EXIT(clap_res);
    }

    /*
     * XXX: this doesn't belong here, same as imgui_render_begin()
     * and any error paths past this point must call the corresponding
     * renderer_frame_end() etc
     */
    renderer_frame_begin(clap_get_renderer(clap_res.val));
    imgui_render_begin(cfg.width, cfg.height);

    struct scene *scene = clap_get_scene(clap_res.val);
    cerr err = subscribe(clap_res.val, MT_INPUT, handle_input, scene);
    if (IS_CERR(err))   goto exit_scene;

    scene->ls = loading_screen_init(clap_get_ui(clap_res.val));

    title_sound = ref_new(sound, .ctx = clap_get_sound(clap_res.val), .name = "brass attack.ogg");
    if (title_sound)    sound_set_gain(title_sound, 0.1);

    fuzzer_input_init(clap_res.val);

    /*
     * The cave goes in before the scene file, whose terrain_clamp character
     * lands on its floor at load; the crystals after it, as their lights
     * must not take slot 0 from the scene file's headlamp, the shadow caster
     */
    CRES_RET(make_cave(scene, NULL), err_cerr(__resp, "no cave\n"));
    CERR_RET(scene_load(scene, "cave.json"), err_cerr(__cerr, "failed to load the scene\n"));
    cave_built = true;
    cave_fill(clap_res.val);

    loading_screen_done(scene->ls);

    scene->lin_speed = 2.0;
    scene->ang_speed = 45.0;
    scene->limbo_height = 70.0;

    imgui_render();
    renderer_frame_end(clap_get_renderer(clap_res.val));
    err = display_main_loop();
    if (IS_CERR(err))   err_cerr(err, "main loop exited with an error\n");
    else                dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    if (title_sound)    ref_put(title_sound);
    noisy_mesh_done();
exit_scene:
    clap_done(clap_res.val, 0);
#else
exit_scene:
    if (IS_CERR(err))   imgui_render();
#endif /* CONFIG_BROWSER */

    return EXIT_SUCCESS;
}
