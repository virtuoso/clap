// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "pipeline.h"
#include "shader_constants.h"
#include <sched.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "object.h"
#include "common.h"
#include "display.h"
#include "input.h"
#include "messagebus.h"
#include "loading-screen.h"
#include "lut.h"
#include "model.h"
#include "ui.h"
#include "scene.h"
#include "sound.h"
#include "physics.h"
#include "presets.h"
#include "networking.h"
#include "ldjam60.h"

/**
 * struct game - what the game keeps on top of the engine's scene
 * @switches:   the floor switches and the rotatables they turn
 * @clappy:     the player's robot
 */
struct game {
    switches    switches;
    clappy      clappy;
};

/* the start menu's and the loading screen's backdrop, and the camera from before it */
static noise_bg menu_bg;
static texture_t menu_bg_color;
static vec3 game_camera_pos, game_camera_angles;

enum main_state {
    MS_LOADING = 0,
    MS_STARTING,
    MS_RUNNING,
    MS_THE_END,
};

static enum main_state main_state;

/*
 * A swirl under the start menu and the loading screen. Its color is its own,
 * so it looks the same under its light and, once "Start Game" hands that
 * light's slot over, under the scene's sun.
 */
static void menu_backdrop_new(struct scene *s)
{
    transform_pos(&s->camera->xform, game_camera_pos);
    transform_rotation(&s->camera->xform, game_camera_angles, true);

    cerr err = texture_pixel_init(clap_get_renderer(s->clap_ctx), &menu_bg_color, (float[]){ 1.0, 0.25, 1.0, 1 });
    if (IS_CERR(err))   err_cerr(err, "couldn't initialize pixel texture\n");

    cres(noise_bg) res = noise_bg_new(s, &(noise_bg_opts){
        .pos                = { 0.0f, 0.0f, 0.0f },
        .extent             = 10.0f,
        .tex                = IS_CERR(err) ? NULL : &menu_bg_color,
        .emission           = { 1.0f, 0.0f, 1.0f },
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

static void menu_backdrop_done(struct scene *s)
{
    noise_bg_done(s, &menu_bg);
    transform_set_pos(&s->camera->xform, game_camera_pos);
    transform_set_angles(&s->camera->xform, game_camera_angles, true);
}

/* The scene is in, or as much of it as would go: set the game up on it and start */
static void scene_loaded(struct scene *s, cerr err, void *data)
{
    struct ui *ui = clap_get_ui(s->clap_ctx);
    struct game *game = data;

    if (IS_CERR(err))   err_cerr(err, "couldn't load the scene\n");

    CERR_RET(switches_resolve(&game->switches), err_cerr(__cerr, "couldn't wire up the switches\n"));
    CERR_RET(clappy_init(&game->clappy, s), err_cerr(__cerr, "couldn't find clappy\n"));

    /* still in UI_ST_LOADING: the loading screen goes without a frame of its own */
    loading_screen_done(s->ls);
    s->ls = NULL;
    menu_backdrop_done(s);
    ui_state_set_running(ui);

    main_state = MS_STARTING;
}

/* "Start Game": the loading screen goes up over the backdrop, and the scene comes in */
static void loading_start(clap_context *ctx, void *data)
{
    struct scene *s = clap_get_scene(ctx);

    /* light slot 0 casts the shadows, and the scene's sun takes the lowest free one */
    if (menu_bg.light_idx >= 0) {
        light_put(&s->light, menu_bg.light_idx);
        menu_bg.light_idx = -1;
    }

    s->ls = loading_screen_init(clap_get_ui(ctx), .skip_background = true);

    cerr err = scene_load_async(s, "level1.json", scene_loaded, data);
    if (IS_CERR(err))   scene_loaded(s, err, data);
}

/*
 * "Restart", for a player who's stuck or wants a rematch: the rotatables
 * go back to the way the scene set them up, and clappy where he originally
 * spawned
 */
static void restart(clap_context *ctx, void *data)
{
    struct game *game = data;

    switches_reset(&game->switches);
    clappy_restart(&game->clappy, clap_get_scene(ctx));
}

static EMSCRIPTEN_KEEPALIVE void render_frame(clap_context *ctx, void *data)
{
    struct scene *s = clap_get_scene(ctx);
    struct game *game = data;

    CERR_RET(clappy_update(&game->clappy, s), err_cerr(__cerr, "clappy's motors are stuck\n"));
    switches_debug(&game->switches);

    if (main_state == MS_STARTING) {
        main_state++;
    } else if (main_state == MS_THE_END) {
        main_state++;
    }
}

static int handle_command(struct clap_context *ctx, struct message *m, void *data)
{
    return 0;
}

static cerr early_init(clap_context *ctx, void *data)
{
    return subscribe(ctx, MT_COMMAND, handle_command, data);
}

static void graphics_init(clap_context *ctx, void *data)
{
    auto ropts = clap_get_render_options(ctx);
    ropts->bloom_intensity = 1.2;
    ropts->bloom_threshold = 0.01;
    ropts->bloom_exposure = 1.35;
    ropts->shadow_outline = false;
    ropts->lighting_operator = 1.0;
    ropts->contrast = 0.2;
    ropts->lighting_exposure = 1.1;
    ropts->taa = true;
    ropts->taa_feedback = 0.05f;
    ropts->edge_sobel = false;
    ropts->laplace_kernel = 5;
    ropts->fog = true;
    ropts->fog_curve = FOG_CURVE_EXP;
    ropts->fog_max = 0.5;
    vec3_dup(ropts->fog_color, (vec3){ 0.05, 0.05, 0.05 });
    ropts->fog_volumetric = true;
    ropts->volumetric_wind = 0.5;
    ropts->film_grain = true;
    ropts->film_grain_factor = 0.02;
    ropts->ssao_radius = 0.5f;
    ropts->ssao_weight = 1.0f;
    ropts->ao_method = AO_METHOD_GTAO;

    CERR_RET(
        clap_set_lighting_lut(ctx, "SATURATE"),
        err_cerr(__cerr, "failed to set LUT\n")
    );

    struct scene *scene = clap_get_scene(ctx);
    auto view = &scene->camera->view;
    scene->camera->dist_override = 3.5f;

    view->main.far_plane = 200.0f;
    clap_reserve_shadow_passes(ctx, view, 3);
}

static cerr node_spawn(struct scene *scene, struct gltf_data *gd, int node, entity3d *e, void *priv)
{
    return switches_node_spawn(scene, gd, node, e, priv);
}

int main(int argc, char **argv, char **envp)
{
    /* static: in a browser, the frames go on after main() returns */
    static struct game game;

    struct clap_config cfg = {
        .debug          = 1,
        .input          = 1,
        .font           = 1,
        .sound          = 1,
        .phys           = 1,
        .graphics       = 1,
        .ui             = 1,
        .ui_menu        = { .enable = true, .restart = restart, .restart_data = &game },
        .ui_start_menu  = { .enable = true, .loading_cb = loading_start, .loading_cb_data = &game },
        .settings       = 1,
        .title          = CLAP_EXECUTABLE_TITLE,
#ifndef CONFIG_BROWSER
        .base_url       = "demo/ldjam60/",
#endif
        .width          = 1280,
        .height         = 720,
#ifndef CONFIG_FINAL
        .networking     = &(struct networking_config) {
            .server_ip     = CONFIG_SERVER_IP,
            .server_port   = CONFIG_SERVER_PORT,
            .server_wsport = CONFIG_SERVER_WSPORT,
            .logger        = 1,
        },
#endif /* CONFIG_FINAL */
        .early_init     = early_init,
        .graphics_init  = graphics_init,
        .frame_cb       = render_frame,
        .callback_data  = &game,
        .node_spawn     = node_spawn,
        .node_spawn_priv    = &game.switches,
        .default_font_name  = CLAP_FONT_FILE,
#ifdef CONFIG_FINAL
        .lut_presets    = (lut_preset[]){ LUT_SATURATE },
#else
        .lut_presets    = lut_presets_all,
#endif /* CONFIG_FINAL */
    };

    cresp(clap_context) clap_res = clap_init(&cfg, argc, argv, envp);
    if (IS_CERR(clap_res)) {
        err_cerr(clap_res, "failed to initialize clap\n");
        return CERR_TO_EXIT(clap_res);
    }

    auto scene = clap_get_scene(clap_res.val);

    switches_init(&game.switches, clap_res.val);

    scene->lin_speed = 1.0;
    scene->ang_speed = 90.0;
    scene->limbo_height = 70.0;

    menu_backdrop_new(scene);
    
    fuzzer_input_init(clap_res.val);

    cerr err = display_main_loop();
    if (IS_CERR(err))   err_cerr(err, "main loop exited with an error\n");
    else                dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    if (scene->ls)  loading_screen_done(scene->ls);

    clappy_done(&game.clappy);
    switches_done(&game.switches);
    clap_done(clap_res.val, 0);
#endif

    return EXIT_SUCCESS;
}
