// SPDX-License-Identifier: Apache-2.0
#include <sched.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include "object.h"
#include "common.h"
#include "display.h"
#include "input.h"
#include "messagebus.h"
#include "loading-screen.h"
#include "lut.h"
#include "model.h"
#include "presets.h"
#include "ui.h"
#include "scene.h"
#include "sound.h"
#include "pipeline-builder.h"
#include "physics.h"
#include "networking.h"
#include "settings.h"
#include "ui-debug.h"

/* XXX just note for the future */
static struct sound *intro_sound;

/* the start menu's and the loading screen's backdrop, and the camera and the grading from before it */
static noise_bg menu_bg;
static texture_t menu_bg_color;
static vec3 game_camera_pos, game_camera_angles;
static float game_contrast, game_lighting_exposure;

static const char *intro_osd[] = {
    "WASD to move the character",
    "Space to jump",
    "Shift to dash",
    "Arrows to move the camera",
    "Have fun"
};

enum main_state {
    MS_LOADING = 0,
    MS_STARTING,
    MS_RUNNING,
};

static enum main_state main_state;

static EMSCRIPTEN_KEEPALIVE void render_frame(clap_context *ctx, void *data)
{
    struct scene *s = clap_get_scene(ctx);
    struct ui *ui = clap_get_ui(s->clap_ctx);

    if (main_state == MS_STARTING) {
        main_state++;
        ui_osd_new(ui, NULL, intro_osd, array_size(intro_osd));
    }
}

static int handle_input(struct clap_context *ctx, struct message *m, void *data)
{
    struct scene *scene = data;
    bool  store = false;

    if (!intro_sound)
        return 0;

    float gain = sound_get_gain(intro_sound);

    if (m->input.volume_up) {
        gain += 0.05;
        sound_set_gain(intro_sound, gain);
        store = true;
    } else if (m->input.volume_down) {
        gain -= 0.05;
        sound_set_gain(intro_sound, gain);
        store = true;
    }

    if (store) {
        struct settings *settings = clap_get_settings(scene->clap_ctx);
        settings_set_num(settings, NULL, "music_volume", gain);
    }
    return 0;
}

static int handle_command(struct clap_context *ctx, struct message *m, void *data)
{
    return 0;
}

static cerr early_init(clap_context *ctx, void *data)
{
    struct scene *scene = data;

    return subscribe(ctx, MT_COMMAND, handle_command, scene);
}

static void graphics_init(clap_context *ctx, void *data)
{
    auto ropts = clap_get_render_options(ctx);
    ropts->fog_near = 20.0;
    ropts->fog_far = 90.0;
    vec3_dup(ropts->fog_color, (vec3){ 0.043, 0.356, 0.369 });
    ropts->lighting_operator = 1.0;
    ropts->contrast = 0.08;
    ropts->bloom_exposure = 10.0;
    ropts->bloom_intensity = 3.0;
    ropts->lighting_exposure = 1.94;
    ropts->edge_sobel = true;
    ropts->edge_antialiasing = false;
    ropts->shadow_outline = false;

    CERR_RET(
        clap_set_lighting_lut(ctx, "scifi bluegreen"),
        err_cerr(__cerr, "failed to set LUT\n")
    );
}

/*
 * A swirl under the start menu and the loading screen. Its color is its own,
 * so it looks the same under its light and, once "Start Game" hands that
 * light's slot over, under the scene's sun.
 */
static void menu_backdrop_new(struct scene *s)
{
    auto ropts = clap_get_render_options(s->clap_ctx);

    transform_pos(&s->camera->xform, game_camera_pos);
    transform_rotation(&s->camera->xform, game_camera_angles, true);
    game_contrast = ropts->contrast;
    game_lighting_exposure = ropts->lighting_exposure;
    ropts->contrast = 1.0f;
    ropts->lighting_exposure = 1.34f;

    cerr err = texture_pixel_init(clap_get_renderer(s->clap_ctx), &menu_bg_color, (float[]){ 0.5, 0.1, 1.0, 1 });
    if (IS_CERR(err))   err_cerr(err, "couldn't initialize pixel texture\n");

    cres(noise_bg) res = noise_bg_new(s, &(noise_bg_opts){
        .pos                = { 0.0f, 0.0f, 0.0f },
        .extent             = 10.0f,
        .tex                = IS_CERR(err) ? nullptr : &menu_bg_color,
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
    auto ropts = clap_get_render_options(s->clap_ctx);

    noise_bg_done(s, &menu_bg);
    transform_set_pos(&s->camera->xform, game_camera_pos);
    transform_set_angles(&s->camera->xform, game_camera_angles, true);
    ropts->contrast = game_contrast;
    ropts->lighting_exposure = game_lighting_exposure;
}

/* The scene is in, or as much of it as would go: drop the loading screen and start */
static void scene_loaded(struct scene *s, cerr err, void *data)
{
    struct ui *ui = clap_get_ui(s->clap_ctx);

    if (IS_CERR(err))   err_cerr(err, "couldn't load the scene\n");

    /* still in UI_ST_LOADING: the loading screen goes without a frame of its own */
    loading_screen_done(s->ls);
    s->ls = nullptr;
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

    cerr err = scene_load_async(s, "scene.json", scene_loaded, nullptr);
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
        .base_url       = "demo/ldjam56/",
#endif
        .width          = 1280,
        .height         = 720,
#ifndef CONFIG_FINAL
        .networking = &(struct networking_config) {
            .server_ip     = CONFIG_SERVER_IP,
            .server_port   = CONFIG_SERVER_PORT,
            .server_wsport = CONFIG_SERVER_WSPORT,
            .logger        = 1,
        },
#endif /* CONFIG_FINAL */
        .early_init     = early_init,
        .graphics_init  = graphics_init,
        .frame_cb       = render_frame,
        .default_font_name  = CLAP_FONT_FILE,
#ifdef CONFIG_FINAL
        .lut_presets    = (lut_preset[]){ LUT_SCIFI_BLUEGREEN },
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
    cerr err = subscribe(clap_res.val, MT_INPUT, handle_input, scene);
    if (IS_CERR(err))
        goto exit_scene;

    /* the frames run while the scene loads: its characters need these from the start */
    scene->lin_speed = 2.0;
    scene->ang_speed = 45.0;
    scene->limbo_height = 70.0;

    menu_backdrop_new(scene);

    intro_sound = ref_new(sound, .ctx = clap_get_sound(clap_res.val), .name = "morning.ogg");
    if (intro_sound) {
        float intro_gain = settings_get_num(clap_get_settings(clap_res.val), NULL, "music_volume");
        sound_set_gain(intro_sound, intro_gain);
        sound_set_looping(intro_sound, true);
        sound_play(intro_sound);
    }

    fuzzer_input_init(clap_res.val);

    err = display_main_loop();
    if (IS_CERR(err))   err_cerr(err, "main loop exited with an error\n");
    else                dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    if (intro_sound)
        ref_put(intro_sound);
exit_scene:
    /* a window closed mid-load leaves the loading screen, to go while the UI is up */
    if (scene->ls)  loading_screen_done(scene->ls);
    /* a static texture, which model3dtx_drop() leaves alone */
    texture_deinit(&menu_bg_color);
    clap_done(clap_res.val, 0);
#else
exit_scene:
#endif

    return EXIT_SUCCESS;
}
