// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <getopt.h>
#include <sched.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
//#include <linux/time.h> /* XXX: for intellisense */
#include <math.h>
#include <unistd.h>
#include "shader_constants.h"
#include "object.h"
#include "common.h"
#include "input.h"
#include "messagebus.h"
#include "librarian.h"
#include "matrix.h"
#include "model.h"
#include "shader.h"
#include "terrain.h"
#include "ui.h"
#include "scene.h"
#include "sound.h"
#include "pipeline.h"
#include "physics.h"
#include "primitives.h"
#include "networking.h"
#include "profiler.h"
#include "settings.h"
#include "ui-debug.h"
#include "game.h"

/* XXX just note for the future */
struct sound *intro_sound;
static int exit_timeout = -1;
struct scene scene; /* XXX */
struct ui ui;
// struct game_state game_state;

struct clap_context *clap_ctx;
struct pipeline *main_pl;
static bool prev_msaa;

static void build_main_pl(struct pipeline **pl)
{
    *pl = pipeline_new(&scene, "main");

    struct render_pass *shadow_pass[CASCADES_MAX];

#ifdef CONFIG_GLES
    int i;
    for (i = 0; i < CASCADES_MAX; i++) {
        shadow_pass[i] = pipeline_add_pass(*pl,
                                           .cascade         = i,
                                           .nr_attachments  = FBO_DEPTH_TEXTURE,
                                           .shader_override = "shadow");
        scene.light.shadow[0][i] = pipeline_pass_get_texture(shadow_pass[i], 0);
    }
#else
    shadow_pass[0] = pipeline_add_pass(*pl,
                                       .multisampled    = scene.light.shadow_msaa,
                                       .nr_attachments  = FBO_DEPTH_TEXTURE,
                                       .shader_override = "shadow");
    scene.light.shadow[0][0] = pipeline_pass_get_texture(shadow_pass[0], 0);
#endif /* CONFIG_GLES */

    struct render_pass *model_pass = pipeline_add_pass(*pl,
                                                       .multisampled   = true,
                                                       .nr_attachments = 3,
                                                       .name           = "model");
    struct render_pass *pass;
    pass = pipeline_add_pass(*pl, .source = model_pass, .shader = "vblur", .blit_from = 1);
    struct render_pass *bloom_pass = pipeline_add_pass(*pl,
                                                       .source     = pass,
                                                       .shader     = "hblur",
                                                       .pingpong   = 5);
    struct render_pass *sobel_pass = pipeline_add_pass(*pl,
                                                       .source     = model_pass,
                                                       .shader     = "sobel",
                                                       .blit_from  = 2);
    pass = pipeline_add_pass(*pl,
                             .source = model_pass,
                             .shader = "combine",
                             .stop   = true);
    pipeline_pass_add_source(*pl, pass, UNIFORM_EMISSION_MAP, bloom_pass, -1);
    pipeline_pass_add_source(*pl, pass, UNIFORM_SOBEL_TEX, sobel_pass, -1);

    pass = pipeline_add_pass(*pl, .source = pass, .shader = "vblur", .name = "menu vblur");
    pass = pipeline_add_pass(*pl,
                             .source   = pass,
                             .shader   = "hblur",
                             .name     = "menu hblur",
                             .pingpong = 5);
}

EMSCRIPTEN_KEEPALIVE void render_frame(void *data)
{
    struct scene *s = data;
    unsigned long count, frame_count;

    frame_count = max((unsigned long)display_refresh_rate() / clap_get_fps_fine(clap_ctx), 1);
    PROF_FIRST(start);

    imgui_render_begin(s->width, s->height);
    fuzzer_input_step();

    scene.ts = clap_get_current_timespec(clap_ctx);

    if (s->control) {
        /*
        * calls into character_move(): handle inputs, adjust velocities etc
        * for the physics step
        */
        scene_characters_move(s);
    }
    PROF_STEP(move, start)

    for (count = 0; count < frame_count; count++)
        phys_step(clap_get_phys(s->clap_ctx), 1);

    PROF_STEP(phys, move);

#ifndef CONFIG_FINAL
    networking_poll();
#endif
    PROF_STEP(net, phys);

    scene_update(s);

    ui_update(&ui);
    PROF_STEP(updates, net);

    /* XXX: this actually goes to ->update() */
    scene_cameras_calc(s);
    PROF_STEP(cameras, updates);

    pipeline_render(main_pl, !ui.modal);
    PROF_STEP(render, cameras);

    if (scene.debug_draws_enabled)
        models_render(&scene.debug_mq, NULL, NULL, scene.camera, &scene.camera->view.main.proj_mx,
                      NULL, scene.width, scene.height, -1, NULL);

    PROF_STEP(debug_draws, render);

    models_render(&ui.mq, NULL, NULL, NULL, NULL, NULL, 0, 0, -1, &count);

    PROF_STEP(ui_render, debug_draws);

    if (prev_msaa != s->light.shadow_msaa) {
        prev_msaa = s->light.shadow_msaa;
        pipeline_put(main_pl);
        build_main_pl(&main_pl);
    }
    pipeline_debug(main_pl);
    profiler_show(PROF_PTR(start));
    imgui_render();

    s->frames_total += frame_count;
    ui.frames_total += frame_count;
    display_swap_buffers();
    debug_draw_clearout(s);
}

#ifdef CONFIG_BROWSER
extern void touch_set_size(int, int);
#else
static void touch_set_size(int w, int h) {}
#endif

/*
 * XXX: this should be a message
 * cmd.resize
 */
void resize_cb(void *data, int width, int height)
{
    struct scene *scene = data;

    if (!scene->initialized)
        return;

    ui.width  = width;
    ui.height = height;
    scene->width  = width;
    scene->height = height;
    if (main_pl)
        pipeline_resize(main_pl);
    touch_set_size(width, height);
    trace("resizing to %dx%d\n", width, height);
    scene->proj_update++;
}

static void ohc_ground_contact(void *priv, float x, float y, float z)
{
    if (scene.auto_yoffset < y)
        scene.auto_yoffset = y;
}

static int handle_input(struct message *m, void *data)
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

static int handle_command(struct message *m, void *data)
{
    if (m->cmd.status && exit_timeout >= 0) {
        if (!exit_timeout--)
            display_request_exit();
    }

    return 0;
}

static struct option long_options[] = {
    { "fullscreen", no_argument,        0, 'F' },
    { "exitafter",  required_argument,  0, 'e' },
    { "aoe",        no_argument,        0, 'E' },
    { "server",     required_argument,  0, 'S'},
    {}
};

static const char short_options[] = "e:EFS:";

int main(int argc, char **argv, char **envp)
{
    struct clap_config cfg = {
        .debug          = 1,
        .input          = 1,
        .font           = 1,
        .sound          = 1,
        .phys           = 1,
        .graphics       = 1,
        .settings       = 1,
        .title          = "One Hand Clap",
#ifndef CONFIG_BROWSER
        .base_url       = "demo/ldjam56/",
#endif
        .width          = 1280,
        .height         = 720,
        .frame_cb       = render_frame,
        .resize_cb      = resize_cb,
        .callback_data  = &scene,
    };
    struct networking_config ncfg = {
        .server_ip     = CONFIG_SERVER_IP,
        .server_port   = 21044,
        .server_wsport = 21045,
        .logger        = 1,
    };
    int c, option_index, err;
    unsigned int fullscreen = 0;
    struct render_pass *pass;
    //struct lib_handle *lh;

    for (;;) {
        c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'F':
            fullscreen++;
            break;
#ifndef CONFIG_FINAL
        case 'e':
            exit_timeout = atoi(optarg);
            break;
        case 'E':
            abort_on_error++;
            break;
        case 'S':
            ncfg.server_ip = optarg;
            break;
#endif /* CONFIG_FINAL */
        default:
            fprintf(stderr, "invalid option %x\n", c);
            exit(EXIT_FAILURE);
        }
    }

    clap_ctx = clap_init(&cfg, argc, argv, envp);

    imgui_render_begin(cfg.width, cfg.height);
    scene_init(&scene);
    scene.clap_ctx = clap_ctx;
    prev_msaa = scene.light.shadow_msaa;

#ifndef CONFIG_FINAL
    ncfg.clap = clap_ctx;
    networking_init(&ncfg, CLIENT);
#endif

    phys_set_ground_contact(clap_get_phys(clap_ctx), ohc_ground_contact);

    subscribe(MT_INPUT, handle_input, &scene);
    subscribe(MT_COMMAND, handle_command, &scene);

    /*
     * Need to write vorbis callbacks for this
     * lib_request(RES_ASSET, "morning.ogg", opening_sound_load, &intro_sound);
     */
    float intro_gain = settings_get_num(clap_get_settings(clap_ctx), NULL, "music_volume");
    intro_sound = sound_load("morning.ogg");
    sound_set_gain(intro_sound, intro_gain);
    sound_set_looping(intro_sound, true);
    sound_play(intro_sound);

    /* Before models are created */
    lib_request_shaders("contrast", &scene.shaders);
    lib_request_shaders("hblur", &scene.shaders);
    lib_request_shaders("vblur", &scene.shaders);
    lib_request_shaders("sobel", &scene.shaders);
    lib_request_shaders("combine", &scene.shaders);
    lib_request_shaders("shadow", &scene.shaders);
    lib_request_shaders("debug", &scene.shaders);
    // lib_request_shaders("terrain", &scene.shaders);
    lib_request_shaders("model", &scene.shaders);
    //lib_request_shaders("ui", &scene);

    fuzzer_input_init();

    if (fullscreen)
        display_enter_fullscreen();

    scene_camera_add(&scene);
    scene.camera = &scene.cameras[0];
    // scene_camera_add(&scene);

    scene_load(&scene, "scene.json");

    /* XXX: fix game_init() */
    //game_init(&scene, &ui); // this must happen after scene_load, because we need the trees.
    // spawn_mushrooms(&game_state);
    // subscribe(MT_INPUT, handle_game_input, NULL);

    display_get_sizes(&scene.width, &scene.height);

    build_main_pl(&main_pl);

    err = ui_init(&ui, scene.width, scene.height);
    if (err)
        goto exit_ui;

    scene.lin_speed = 2.0;
    scene.ang_speed = 45.0;
    scene.limbo_height = -70.0;
    scene_cameras_calc(&scene);

    imgui_render();
    display_main_loop();

    dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    pipeline_put(main_pl);
    ui_done(&ui);
exit_ui:
    scene_done(&scene);
    //gl_done();
    clap_done(clap_ctx, 0);
#else
exit_ui:
    if (err)
        imgui_render();
#endif

    return EXIT_SUCCESS;
}
