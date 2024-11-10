// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <getopt.h>
#include <sched.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
//#include <linux/time.h> /* XXX: for intellisense */
#include <math.h>
#include <unistd.h>
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
#include "settings.h"
#include "ui-debug.h"
#include "game.h"

/* XXX just note for the future */
struct settings *settings;
struct sound *intro_sound;
static int exit_timeout = -1;
struct scene scene; /* XXX */
struct ui ui;
// struct game_state game_state;

#ifndef CONFIG_FINAL
#define PROFILER
#endif

#ifdef PROFILER
struct profile {
    struct timespec ts, diff;
    const char      *name;
};
#define DECLARE_PROF(_n) \
    struct profile prof_ ## _n = { .name = __stringify(_n) }

DECLARE_PROF(start);
DECLARE_PROF(phys);
DECLARE_PROF(net);
DECLARE_PROF(updates);
DECLARE_PROF(models);
DECLARE_PROF(ui);
DECLARE_PROF(end);

#define PROF_FIRST(_n) \
    clock_gettime(CLOCK_MONOTONIC, &prof_ ## _n.ts);

#define PROF_STEP(_n, _prev) \
    clock_gettime(CLOCK_MONOTONIC, &prof_ ## _n.ts); \
    timespec_diff(&prof_ ## _prev.ts, &prof_ ## _n.ts, &prof_ ## _n.diff);

#define PROF_SHOW(_n) \
    dbg("PROFILER: '%s': %lu.%09lu\n", __stringify(_n), prof_ ## _n.diff.tv_sec, prof_ ## _n.diff.tv_nsec);
#else
#define DECLARE_PROF(x)
#define PROF_FIRST(x)
#define PROF_STEP(x,y)
#define PROF_SHOW(x)
#endif

struct clap_context *clap_ctx;
struct pipeline *main_pl, *blur_pl;

EMSCRIPTEN_KEEPALIVE void render_frame(void *data)
{
    struct timespec ts_start, ts_delta;
    struct scene *s = data; /* XXX */
    unsigned long count, frame_count;
    float y0, y1, y2;
    dReal by;

    clap_fps_calc(clap_ctx, &s->fps);
    ts_start = clap_get_current_timespec(clap_ctx);
    ts_delta = s->fps.ts_delta;

    frame_count = max((unsigned long)gl_refresh_rate() / s->fps.fps_fine, 1);
    PROF_FIRST(start);

    imgui_render_begin(s->width, s->height);
    fuzzer_input_step();

    /* XXX: fix game_init() */
    // game_update(&game_state, ts_start, ui.modal);

    scene.ts = ts_start;

    if (s->control) {
        /*
        * calls into character_move(): handle inputs, adjust velocities etc
        * for the physics step
        */
        y0 = s->control->entity->dy;
        scene_characters_move(s);

        /*
        * Collisions, dynamics
        */
        y1 = s->control->entity->dy;
    }

    for (count = 0; count < frame_count; count++)
        phys_step(1);

    PROF_STEP(phys, start);

#ifndef CONFIG_FINAL
    networking_poll();
#endif
    PROF_STEP(net, phys);

    if (s->control) {
        /*
        * calls entity3d_update() -> character_update()
        */
        y2 = s->control->entity->dy;
        scene_update(s);
        if (s->control->entity->phys_body) {
            const dReal *pos = phys_body_position(s->control->entity->phys_body);
            by = pos[1];
        } else {
            by = y2;
        }
    }

    // ui_debug_printf("'%s': (%f,%f,%f)\nyoff: %f ray_off: %f\n%f+ %f +%f +%f || %f\n%s%s",
    //                 s->control->entity ? entity_name(s->control->entity) : "", s->control->entity->dx, s->control->entity->dy,
    //                 s->control->entity->dz, s->control->entity->phys_body ? s->control->entity->phys_body->yoffset : 0,
    //                 s->control->entity->phys_body ? s->control->entity->phys_body->ray_off : 0,
    //                 y0, y1-y0, y2-y1, s->control->entity->dy - y2, by,
    //                 s->control->ragdoll ? "ragdoll " : "", s->control->stuck ? "stuck" : "");
    ui_update(&ui);
    PROF_STEP(updates, net);

    /* XXX: this actually goes to ->update() */
    scene_cameras_calc(s);

#ifndef CONFIG_GLES
    glEnable(GL_MULTISAMPLE);
#endif
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    if (ui.modal)
        pipeline_render(blur_pl);
    else
        pipeline_render(main_pl);

    PROF_STEP(models, updates);

    s->proj_updated = 0;
    models_render(&ui.mq, NULL, NULL, NULL, NULL, 0, 0, &count);
    imgui_render();
    PROF_STEP(ui, models);

    s->frames_total += frame_count;
    ui.frames_total += frame_count;
    gl_swap_buffers();
    PROF_STEP(end, ui);
#ifndef CONFIG_FINAL
    ui_debug_printf(
        "phys:    %" PRItvsec ".%09lu\n"
        "net:     %" PRItvsec ".%09lu\n"
        "updates: %" PRItvsec ".%09lu\n"
        "models:  %" PRItvsec ".%09lu\n"
        "ui:      %" PRItvsec ".%09lu\n"
        "end:     %" PRItvsec ".%09lu\n"
        "ui_entities: %lu\n%s",
        prof_phys.diff.tv_sec, prof_phys.diff.tv_nsec,
        prof_net.diff.tv_sec, prof_net.diff.tv_nsec,
        prof_updates.diff.tv_sec, prof_updates.diff.tv_nsec,
        prof_models.diff.tv_sec, prof_models.diff.tv_nsec,
        prof_ui.diff.tv_sec, prof_ui.diff.tv_nsec,
        prof_end.diff.tv_sec, prof_end.diff.tv_nsec,
        count, ref_classes_get_string()
    );
#endif
    debug_draw_clearout(s);
}

#define FOV to_radians(70.0)

static void projmx_update(struct scene *s)
{
    struct matrix4f *m = s->proj_mx;
    float y_scale = (1 / tan(FOV / 2));
    float x_scale = y_scale / s->aspect;
    float frustum_length = s->far_plane - s->near_plane;

    m->cell[0] = x_scale;
    m->cell[5] = y_scale;
    m->cell[10] = -((s->far_plane + s->near_plane) / frustum_length);
    m->cell[11] = -1;
    m->cell[14] = -((2 * s->near_plane * s->far_plane) / frustum_length);
    m->cell[15] = 0;
    s->proj_updated++;
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
    touch_set_size(width, height);
    scene->aspect = (float)width / (float)height;
    trace("resizing to %dx%d\n", width, height);
    glViewport(0, 0, ui.width, ui.height);
    projmx_update(scene);
}

static void ohc_ground_contact(void *priv, float x, float y, float z)
{
    if (scene.auto_yoffset < y)
        scene.auto_yoffset = y;
}

static void settings_onload(struct settings *rs, void *data)
{
    float gain = settings_get_num(rs, "music_volume");

    sound_set_gain(intro_sound, gain);

    imgui_set_settings(rs);
}

static int handle_input(struct message *m, void *data)
{
    float gain = sound_get_gain(intro_sound);
    bool  store = false;

    if (!intro_sound)
        return 0;

    if (m->input.volume_up) {
        gain += 0.05;
        sound_set_gain(intro_sound, gain);
        store = true;
    } else if (m->input.volume_down) {
        gain -= 0.05;
        sound_set_gain(intro_sound, gain);
        store = true;
    }

    if (store)
        settings_set_num(settings, "music_volume", gain);
    return 0;
}

static int handle_command(struct message *m, void *data)
{
    struct scene *scene = data;

    if (m->cmd.status && exit_timeout >= 0) {
        if (!exit_timeout--)
            gl_request_exit();
    }

    return 0;
}

static struct option long_options[] = {
    { "autopilot",  no_argument,        0, 'A' },
    { "fullscreen", no_argument,        0, 'F' },
    { "exitafter",  required_argument,  0, 'e' },
    { "aoe",        no_argument,        0, 'E' },
    { "server",     required_argument,  0, 'S'},
    {}
};

static const char short_options[] = "Ae:EFS:";

int main(int argc, char **argv, char **envp)
{
    struct clap_config cfg = {
        .debug          = 1,
        .input          = 1,
        .font           = 1,
        .sound          = 1,
        .phys           = 1,
        .graphics       = 1,
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
        case 'A':
            scene.autopilot = 1;
            break;
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

    /*
     * Resize callback will call into projmx_update(), which depends
     * on projection matrix being allocated.
     */
    imgui_render_begin(cfg.width, cfg.height);
    scene_init(&scene);

#ifndef CONFIG_FINAL
    ncfg.clap = clap_ctx;
    networking_init(&ncfg, CLIENT);
#endif

    phys->ground_contact = ohc_ground_contact;

    subscribe(MT_INPUT, handle_input, NULL);
    subscribe(MT_COMMAND, handle_command, &scene);

    /*
     * Need to write vorbis callbacks for this
     * lib_request(RES_ASSET, "morning.ogg", opening_sound_load, &intro_sound);
     */
    intro_sound = sound_load("morning.ogg");
    settings = settings_init(settings_onload, NULL);
    sound_set_gain(intro_sound, 0);
    sound_set_looping(intro_sound, true);
    sound_play(intro_sound);

    /* Before models are created */
    lib_request_shaders("contrast", &scene.shaders);
    lib_request_shaders("hblur", &scene.shaders);
    lib_request_shaders("vblur", &scene.shaders);
    lib_request_shaders("sobel", &scene.shaders);
    lib_request_shaders("combine", &scene.shaders);
    lib_request_shaders("debug", &scene.shaders);
    // lib_request_shaders("terrain", &scene.shaders);
    lib_request_shaders("model", &scene.shaders);
    //lib_request_shaders("ui", &scene);

    fuzzer_input_init();

    if (fullscreen)
        gl_enter_fullscreen();

    scene_camera_add(&scene);
    scene.camera = &scene.cameras[0];
    // scene_camera_add(&scene);

    scene_load(&scene, "scene.json");

    /* XXX: fix game_init() */
    //game_init(&scene, &ui); // this must happen after scene_load, because we need the trees.
    // spawn_mushrooms(&game_state);
    // subscribe(MT_INPUT, handle_game_input, NULL);

    gl_get_sizes(&scene.width, &scene.height);

    blur_pl = pipeline_new(&scene, "menu");
    struct render_pass *model_pass = pipeline_add_pass(blur_pl, NULL, NULL, true, 3, 0);
    pipeline_pass_set_name(model_pass, "model");
    pass = pipeline_add_pass(blur_pl, model_pass, "contrast", false, 0, 0);
    struct render_pass *rep_pass = pipeline_add_pass(blur_pl, pass, "vblur", false, 0, 0);
    pass = pipeline_add_pass(blur_pl, rep_pass, "hblur", false, 0, 0);
    pipeline_pass_repeat(pass, rep_pass, 5);

    main_pl = pipeline_new(&scene, "main");
    model_pass = pipeline_add_pass(main_pl, NULL, NULL, true, 3, -1);
    pipeline_pass_set_name(model_pass, "model");
    pass = pipeline_add_pass(main_pl, model_pass, "contrast", false, 0, 1);
    pass = pipeline_add_pass(main_pl, pass, "vblur", false, 0, -1);
    struct render_pass *bloom_pass = pipeline_add_pass(main_pl, pass, "hblur", false, 0, -1);
    pipeline_pass_repeat(bloom_pass, pass, 5);

    // struct render_pass *contrast_pass = pipeline_add_pass(main_pl, model_pass, "contrast", false, 0, 2);
    struct render_pass *sobel_pass = pipeline_add_pass(main_pl, model_pass, "sobel", false, 0, 2);
    pass = pipeline_add_pass(main_pl, model_pass, "combine", false, 0, 0);
    pipeline_pass_add_source(main_pl, pass, 2, bloom_pass, -1);
    pipeline_pass_add_source(main_pl, pass, 3, sobel_pass, -1);

    err = ui_init(&ui, scene.width, scene.height);
    if (err)
        goto exit_ui;

    scene.lin_speed = 2.0;
    scene.ang_speed = 45.0;
    scene.limbo_height = -70.0;
    scene_cameras_calc(&scene);

    imgui_render();
    gl_main_loop();

    dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    ref_put(blur_pl);
    ref_put(main_pl);
    ui_done(&ui);
exit_ui:
    scene_done(&scene);
    settings_done(settings);
    //gl_done();
    clap_done(clap_ctx, 0);
#else
exit_ui:
    if (err)
        imgui_render();
#endif

    return EXIT_SUCCESS;
}
