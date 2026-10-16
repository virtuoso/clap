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
#include "presets.h"
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

/* the start menu's and the loading screen's backdrop, and the camera and the grading from before it */
static noise_bg menu_bg;
static texture_t menu_bg_color;
static vec3 game_camera_pos, game_camera_angles;
static float game_contrast, game_lighting_exposure;

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
 * @MS_LOADING:     the start menu, then the scene's load
 * @MS_STARTING:    the scene is in, before the next frame
 * @MS_RUNNING:     the intro OSD is up
 */
enum main_state {
    MS_LOADING = 0,
    MS_STARTING,
    MS_RUNNING,
};

static enum main_state main_state;

/**
 * render_frame() - frame callback: show the intro OSD once the scene is in
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

    /* the start menu's keys aren't the player's first move */
    if (main_state == MS_LOADING)   return 0;

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
 * Natively the settings load in clap_init(), before the scene file is in;
 * in a browser they arrive from storage after main() has returned, and
 * the cave fills up with whichever comes last.
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

/**
 * menu_backdrop_new() - put a swirl under the start menu and the loading screen
 * @s:  the scene
 *
 * Its color is its own, so it looks the same under its light and, once
 * "Start Game" hands that light's slot over, under the headlamp. The game's
 * grading washes it out flat, so the contrast and the exposure come down
 * while it's up.
 */
static void menu_backdrop_new(struct scene *s)
{
    render_options *ropts = clap_get_render_options(s->clap_ctx);

    transform_pos(&s->camera->xform, game_camera_pos);
    transform_rotation(&s->camera->xform, game_camera_angles, true);
    game_contrast = ropts->contrast;
    game_lighting_exposure = ropts->lighting_exposure;
    ropts->contrast = 1.0f;
    ropts->lighting_exposure = 1.0f;

    cerr err = texture_pixel_init(clap_get_renderer(s->clap_ctx), &menu_bg_color, (float[]){ 0.05, 0.15, 0.6, 1 });
    if (IS_CERR(err))   err_cerr(err, "couldn't initialize pixel texture\n");

    cres(noise_bg) res = noise_bg_new(s, &(noise_bg_opts){
        .pos                = { 0.0f, 0.0f, 0.0f },
        .extent             = 10.0f,
        .tex                = IS_CERR(err) ? nullptr : &menu_bg_color,
        .emission           = { 0.0f, 0.6f, 1.0f },
        .noise_scale        = 0.05f,
        .noise_amp          = 0.8f,
        .noise_drift        = { 0.5f, 0.5f, 0.5f },
        .bloom_threshold    = 0.85f,
        .bloom_intensity    = 0.34f,

        .add_light          = true,
        .light_pos          = { 0.0f, 2.5f, 0.0f },
        .light_dir          = { 0.0f, 1.0f, 0.0f },
        .light_color        = { 0.8f, 0.8f, 0.8f },

        .reposition_camera  = true,
        .camera_pos         = { 0.0f, 2.5f, 0.0f },
        .camera_angles      = { -90.0f, 0.0f, 0.0f },
    });
    if (IS_CERR(res)) {
        err_cerr(res, "couldn't make the menu backdrop\n");
        return;
    }

    menu_bg = res.val;
}

/**
 * menu_backdrop_done() - take the swirl down, put the camera and the grading back
 * @s:  the scene
 */
static void menu_backdrop_done(struct scene *s)
{
    render_options *ropts = clap_get_render_options(s->clap_ctx);

    noise_bg_done(s, &menu_bg);
    transform_set_pos(&s->camera->xform, game_camera_pos);
    transform_set_angles(&s->camera->xform, game_camera_angles, true);
    ropts->contrast = game_contrast;
    ropts->lighting_exposure = game_lighting_exposure;
}

/**
 * scene_loaded() - scene_load_async() callback: fill the cave and start
 * @s:      the scene
 * @err:    the load's outcome
 * @data:   callback data
 *
 * The scene is in, or as much of it as would go. The crystals take their
 * lights now, after the scene file's: its headlamp, the shadow caster,
 * has slot 0.
 */
static void scene_loaded(struct scene *s, cerr err, void *data)
{
    struct ui *ui = clap_get_ui(s->clap_ctx);

    if (IS_CERR(err))   err_cerr(err, "failed to load the scene\n");

    cave_built = true;
    cave_fill(s->clap_ctx);
    CERR_RET(fog_init(s), err_cerr(__cerr, "failed to create the fog\n"));

    /* still in UI_ST_LOADING: the loading screen goes without a frame of its own */
    loading_screen_done(s->ls);
    s->ls = nullptr;
    menu_backdrop_done(s);
    ui_state_set_running(ui);

    main_state = MS_STARTING;
}

/**
 * loading_start() - "Start Game": build the cave and load the scene file
 * @ctx:    clap context
 * @data:   callback data
 *
 * The loading screen goes up over the swirl. The cave goes in before the
 * scene file, whose terrain_clamp character lands on its floor at load.
 */
static void loading_start(clap_context *ctx, void *data)
{
    struct scene *s = clap_get_scene(ctx);

    /* light slot 0 casts the shadows, and the headlamp takes the lowest free one */
    if (menu_bg.light_idx >= 0) {
        light_put(&s->light, menu_bg.light_idx);
        menu_bg.light_idx = -1;
    }

    s->ls = loading_screen_init(clap_get_ui(ctx), .skip_background = true);

    CRES_RET(make_cave(s, NULL), err_cerr(__resp, "no cave\n"));

    cerr err = scene_load_async(s, "cave.json", scene_loaded, NULL);
    if (IS_CERR(err))   scene_loaded(s, err, nullptr);
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
        .ui_start_menu  = { .enable = true, .loading_cb = loading_start },
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

    struct scene *scene = clap_get_scene(clap_res.val);
    cerr err = subscribe(clap_res.val, MT_INPUT, handle_input, scene);
    if (IS_CERR(err))   goto exit_scene;

    /* the frames run while the scene loads: its characters need these from the start */
    scene->lin_speed = 2.0;
    scene->ang_speed = 45.0;
    scene->limbo_height = 70.0;

    menu_backdrop_new(scene);

    title_sound = ref_new(sound, .ctx = clap_get_sound(clap_res.val), .name = "brass attack.ogg");
    if (title_sound)    sound_set_gain(title_sound, 0.1);

    fuzzer_input_init(clap_res.val);

    err = display_main_loop();
    if (IS_CERR(err))   err_cerr(err, "main loop exited with an error\n");
    else                dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    if (title_sound)    ref_put(title_sound);
    fog_done();
    noisy_mesh_done();
exit_scene:
    /* a window closed mid-load leaves the loading screen, to go while the UI is up */
    if (scene->ls)  loading_screen_done(scene->ls);
    /* a static texture, which model3dtx_drop() leaves alone */
    texture_deinit(&menu_bg_color);
    clap_done(clap_res.val, 0);
#else
exit_scene:
#endif /* CONFIG_BROWSER */

    return EXIT_SUCCESS;
}
