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
static particle_system *spores;
static noise_bg      menu_bg;

static const char *intro_osd[] = {
    "WASD to move the character",
    "Space to jump",
    "Shift to dash",
    "Arrows to move the camera",
    "Have fun"
};

enum main_state {
    MS_STARTING = 0,
    MS_RUNNING,
};

static enum main_state main_state;
static unsigned long frame;

static void graphics_init(clap_context *ctx, void *data);

static EMSCRIPTEN_KEEPALIVE void render_frame(clap_context *ctx, void *data)
{
    struct scene *s = clap_get_scene(ctx);
    struct ui *ui = clap_get_ui(s->clap_ctx);

    /* Swirl the menu background while the start menu / loading screen is up. */
    if (ui_state_get(ui) < UI_ST_RUNNING) {
        double dt = clap_get_fps_delta(ctx).tv_nsec / (double)NSEC_PER_SEC;
        clap_get_render_options(ctx)->noise_shift += (float)(dt * 0.5);
    }

    if (s->ls && ui_state_get(ui) == UI_ST_LOADING) {
        if (frame++ < 400)  { loading_screen_progress(s->ls, (float)frame / 400.0); }
        else                {
            loading_screen_done(s->ls);
            s->ls = nullptr;
            noise_bg_done(s, &menu_bg);
            ui_state_set_running(ui);
            scene_load(s, "scene.json");
            transform_set_angles(&s->camera->xform, (vec3){ 0.0, 180.0, 0.0 }, true);
            graphics_init(ctx, data);
        }
    }

    if (clap_is_paused(ctx))    return;

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
    ropts->fog_near = 300.0;
    ropts->fog_far = 300.0;
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

static void loading_start(clap_context *clap_ctx, void *data)
{
    auto scene = clap_get_scene(clap_ctx);
    auto ui = clap_get_ui(clap_ctx);

    scene->ls = loading_screen_init(ui, .skip_background = true);
}

static void make_menu_quad(struct scene *scene)
{
    auto ropts = clap_get_render_options(scene->clap_ctx);
    ropts->film_grain = false;
    ropts->contrast = 1.0f;
    ropts->lighting_exposure = 1.34f;

    cres(noise_bg) res = noise_bg_new(scene, &(noise_bg_opts){
        .pos                = { 0.0f, 0.0f, 0.0f },
        .extent             = 10.0f,
        .emission           = { 1.0f, 0.0f, 1.0f },
        .noise_scale        = 0.05f,
        .noise_amp          = 0.8f,
        .bloom_threshold    = 0.85f,
        .bloom_intensity    = 0.34f,

        .add_light          = true,
        .light_pos          = { 0.0f, 2.5f, 0.0f },
        .light_dir          = { 0.0f, 1.0f, 0.0f },
        .light_color        = { 0.5f, 0.1f, 1.0f },

        .reposition_camera  = true,
        .camera_pos         = { 0.0f, 2.5f, 0.0f },
        .camera_angles      = { -90.0f, 0.0f, 0.0f },
    });
    if (IS_CERR(res))   return;
    menu_bg = res.val;

    /* tweak the material roughness/metallic that the preset doesn't expose */
    menu_bg.entity->txmodel->mat.roughness = 0.8f;
    menu_bg.entity->txmodel->mat.metallic  = 0.8f;
}

static cerr startup(struct scene *scene)
{
    auto prog = CRES_RET_CERR(pipeline_shader_find_get(clap_get_pipeline(scene->clap_ctx), "particle"));
    spores = CRES_RET_CERR(
        ref_new_checked(
            particle_system,
            .name   = "spores",
            .prog   = ref_pass(prog),
            .mq     = &scene->mq,
            .dist   = PART_DIST_POW075,
            .emit   = white_pixel(),
            .count  = 512,
            .radius = 100.0f,
            .scale  = 0.08,
            .bloom_intensity    = 0.1,
            .min_radius         = 20.0f,
        )
    );

    return CERR_OK;
}

static __unused void cleanup(struct scene *scene)
{
    ref_put_last(spores);
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
        .base_url       = "demo/ldjam59/",
#endif
        .width          = 1280,
        .height         = 720,
#ifndef CONFIG_FINAL
        .networking = &(struct networking_config) {
            .server_ip     = CONFIG_SERVER_IP,
            .server_port   = 21044,
            .server_wsport = 21045,
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
    CERR_RET(subscribe(clap_res.val, MT_INPUT, handle_input, scene), goto exit_scene);

    make_menu_quad(scene);
    cerr err = startup(scene);
    err_cerr(err, "startup failed\n");

    intro_sound = ref_new(sound, .ctx = clap_get_sound(clap_res.val), .name = "morning.ogg");
    if (intro_sound) {
        float intro_gain = settings_get_num(clap_get_settings(clap_res.val), NULL, "music_volume");
        sound_set_gain(intro_sound, intro_gain);
        sound_set_looping(intro_sound, true);
        sound_play(intro_sound);
    }

    fuzzer_input_init(clap_res.val);

    scene->lin_speed = 2.0;
    scene->ang_speed = 45.0;
    scene->limbo_height = 70.0;

    display_main_loop();

    dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    if (intro_sound)
        ref_put(intro_sound);
    cleanup(scene);
exit_scene:
    clap_done(clap_res.val, 0);
#else
exit_scene:
#endif

    return EXIT_SUCCESS;
}
