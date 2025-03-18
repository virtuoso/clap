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
#include "model.h"
#include "shader.h"
#include "terrain.h"
#include "ui.h"
#include "scene.h"
#include "sound.h"
#include "pipeline-builder.h"
#include "physics.h"
#include "primitives.h"
#include "networking.h"
#include "profiler.h"
#include "settings.h"
#include "ui-debug.h"
#include "game.h"

/* XXX just note for the future */
static struct sound *intro_sound;
static int exit_timeout = -1;
static struct scene scene; /* XXX */
// struct game_state game_state;

static struct pipeline *main_pl;
static bool shadow_msaa, model_msaa, edge_sobel;

static void build_main_pl(struct pipeline **pl)
{
    *pl = pipeline_build(&(pipeline_builder_opts) {
        .pl_opts = &(pipeline_init_options) {
                  .width            = scene.width,
                  .height           = scene.height,
                  .light            = &scene.light,
                  .camera           = &scene.cameras[0],
                  .renderer         = clap_get_renderer(scene.clap_ctx),
                  .render_options   = &scene.render_options,
                  .shaders          = &scene.shaders,
                  .name             = "main"
        },
        .mq     = &scene.mq,
    });
}

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

EMSCRIPTEN_KEEPALIVE void render_frame(void *data)
{
    struct scene *s = data;
    renderer_t *r = clap_get_renderer(s->clap_ctx);
    struct ui *ui = clap_get_ui(s->clap_ctx);

    if (main_state == MS_STARTING) {
        main_state++;
        ui_osd_new(ui, intro_osd, array_size(intro_osd));
    }

    pipeline_render(main_pl, ui->modal ? 1 : 0);

    if (scene.render_options.debug_draws_enabled) {
        models_render(r, &scene.debug_mq, .camera = scene.camera, .cascade = -1);
        debug_draw_clearout(s);
    }

    if (shadow_msaa != s->render_options.shadow_msaa ||
        model_msaa != s->render_options.model_msaa ||
        edge_sobel != s->render_options.edge_sobel) {
        shadow_msaa = s->render_options.shadow_msaa;
        model_msaa = s->render_options.model_msaa;
        edge_sobel = s->render_options.edge_sobel;
        ref_put(main_pl);
        build_main_pl(&main_pl);
    }
    pipeline_debug(main_pl);
}

/*
 * XXX: this should be a message
 * cmd.resize
 */
void resize_cb(void *data, int width, int height)
{
    struct scene *scene = data;

    if (!scene->initialized)
        return;

    if (main_pl)
        pipeline_resize(main_pl, width, height);
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
        .ui             = 1,
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
        .default_font_name  = "ofl/Unbounded-Regular.ttf",
    };
    struct networking_config ncfg = {
        .server_ip     = CONFIG_SERVER_IP,
        .server_port   = 21044,
        .server_wsport = 21045,
        .logger        = 1,
    };
    int c, option_index;
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

    cresp(clap_context) clap_res = clap_init(&cfg, argc, argv, envp);
    if (IS_CERR(clap_res)) {
        err_cerr(clap_res, "failed to initialize clap\n");
        return EXIT_FAILURE;
    }

    imgui_render_begin(cfg.width, cfg.height);
    scene_init(&scene);
    scene.clap_ctx = clap_res.val;

    cerr err;
#ifndef CONFIG_FINAL
    ncfg.clap = clap_res.val;
    err = networking_init(&ncfg, CLIENT);
    if (IS_CERR(err))
        err_cerr(err, "failed to initialize networking\n");
#endif

    phys_set_ground_contact(clap_get_phys(scene.clap_ctx), ohc_ground_contact);

    err = subscribe(MT_INPUT, handle_input, &scene);
    if (IS_CERR(err))
        goto exit_scene;

    err = subscribe(MT_COMMAND, handle_command, &scene);
    if (IS_CERR(err))
        goto exit_scene;

    /*
     * Need to write vorbis callbacks for this
     * lib_request(RES_ASSET, "morning.ogg", opening_sound_load, &intro_sound);
     */
    intro_sound = ref_new(sound, .ctx = clap_get_sound(scene.clap_ctx), .name = "morning.ogg");
    if (intro_sound) {
        float intro_gain = settings_get_num(clap_get_settings(scene.clap_ctx), NULL, "music_volume");
        sound_set_gain(intro_sound, intro_gain);
        sound_set_looping(intro_sound, true);
        sound_play(intro_sound);
    }

    /* Before models are created */
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "contrast", &scene.shaders);
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "hblur", &scene.shaders);
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "vblur", &scene.shaders);
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "downsample", &scene.shaders);
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "upsample", &scene.shaders);
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "sobel", &scene.shaders);
#ifndef CONFIG_GLES
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "sobel-msaa", &scene.shaders);
#endif /* CONFIG_GLES */
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "laplace", &scene.shaders);
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "combine", &scene.shaders);
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "shadow", &scene.shaders);
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "debug", &scene.shaders);
    // lib_request_shaders("terrain", &scene.shaders);
    lib_request_shaders(clap_get_shaders(scene.clap_ctx), "model", &scene.shaders);
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

    scene.lin_speed = 2.0;
    scene.ang_speed = 45.0;
    scene.limbo_height = -70.0;
    scene_cameras_calc(&scene);

    imgui_render();
    display_main_loop();

    dbg("exiting peacefully\n");

exit_pl:
#ifndef CONFIG_BROWSER
    ref_put(main_pl);
exit_scene:
    scene_done(&scene);
    //gl_done();
    clap_done(scene.clap_ctx, 0);
#else
exit_scene:
    if (IS_CERR(err))
        imgui_render();
#endif

    return EXIT_SUCCESS;
}
