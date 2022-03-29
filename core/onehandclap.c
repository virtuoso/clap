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
#include "display.h"
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
#include "networking.h"
#include "settings.h"
#include "ui-debug.h"

/* XXX just note for the future */
struct settings *settings;
struct sound *intro_sound;
struct scene scene; /* XXX */
struct ui ui;
struct fbo *fbo;

#define PROFILER
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

struct pipeline *main_pl, *blur_pl;

EMSCRIPTEN_KEEPALIVE void renderFrame(void *data)
{
    struct timespec ts_start;
    struct scene *s = data; /* XXX */
    unsigned long count, frame_count;
    float y0, y1, y2;
    dReal by;

    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    clap_fps_calc(&s->fps);
    frame_count = max((unsigned long)gl_refresh_rate() / s->fps.fps_fine, 1);
    PROF_FIRST(start);

    fuzzer_input_step();

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
    phys_step(frame_count);

    PROF_STEP(phys, start);

    networking_poll();
    PROF_STEP(net, phys);

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

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.2f, 0.2f, 0.6f, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    if (ui.modal)
        pipeline_render(blur_pl);
    else
        pipeline_render(main_pl);

    PROF_STEP(models, updates);

    s->proj_updated = 0;
    //glDisable(GL_DEPTH_TEST);
    //glClear(GL_DEPTH_BUFFER_BIT);
    models_render(&ui.mq, NULL, NULL, NULL, NULL, 0, 0, &count);
    PROF_STEP(ui, models);

    s->frames_total += frame_count;
    ui.frames_total += frame_count;
    gl_swap_buffers();
    PROF_STEP(end, ui);
    ui_debug_printf(
        "phys:    %lu.%09lu\n"
        "net:     %lu.%09lu\n"
        "updates: %lu.%09lu\n"
        "models:  %lu.%09lu\n"
        "ui:      %lu.%09lu\n"
        "end:     %lu.%09lu\n"
        "ui_entities: %lu\n%s",
        prof_phys.diff.tv_sec, prof_phys.diff.tv_nsec,
        prof_net.diff.tv_sec, prof_net.diff.tv_nsec,
        prof_updates.diff.tv_sec, prof_updates.diff.tv_nsec,
        prof_models.diff.tv_sec, prof_models.diff.tv_nsec,
        prof_ui.diff.tv_sec, prof_ui.diff.tv_nsec,
        prof_end.diff.tv_sec, prof_end.diff.tv_nsec,
        count, ref_classes_get_string()
    );
}

#define FOV to_radians(70.0)
#define NEAR_PLANE 0.1
#define FAR_PLANE 1000.0

static void projmx_update(struct scene *s)
{
    struct matrix4f *m = s->proj_mx;
    float y_scale = (1 / tan(FOV / 2)) * s->aspect;
    float x_scale = y_scale / s->aspect;
    float frustum_length = FAR_PLANE - NEAR_PLANE;

    m->cell[0] = x_scale;
    m->cell[5] = y_scale;
    m->cell[10] = -((FAR_PLANE + NEAR_PLANE) / frustum_length);
    m->cell[11] = -1;
    m->cell[14] = -((2 * NEAR_PLANE * FAR_PLANE) / frustum_length);
    m->cell[15] = 0;
    s->proj_updated++;
}

static void fbo_update(int width, int height)
{
    if (fbo) {
        ref_put(fbo);
        fbo = NULL;
    }

    if (!ui.prog)
        return;
    if (width > height) {
        width /= 2;
    } else {
        height /= 2;
    }
    if (fbo)
        fbo_resize(fbo, width, height);
    else
        fbo = fbo_new(width, height);
    ui_pip_update(&ui, fbo);
}

#ifdef CONFIG_BROWSER
extern void touch_set_size(int, int);
#else
static void touch_set_size(int w, int h) {}
#endif

/* XXX: this should be a message */
void resize_cb(int width, int height)
{
    ui.width  = width;
    ui.height = height;
    // if (width > height) {
    //     width /= 2;
    // } else {
    //     height /= 2;
    // }
    scene.width  = width;
    scene.height = height;
    touch_set_size(width, height);
    scene.aspect = (float)width / (float)height;
    trace("resizing to %dx%d\n", width, height);
    glViewport(0, 0, ui.width, ui.height);
    projmx_update(&scene);
    // fbo_update(width, height);
}

/*struct model_config {
    const char  *name;
    const char  *texture;
};

struct scene_config {
    struct model_config model[];
} = {
    model   = [
        { .name = "f-16.obj", .texture = "purple.png" },
        { .name = "f-16.obj", .texture = "purple.png" },
    ],
};*/

static void ohc_ground_contact(void *priv, float x, float y, float z)
{
    if (scene.auto_yoffset < y)
        scene.auto_yoffset = y;
}

int font_init(void);

static void settings_onload(struct settings *rs, void *data)
{
    float gain = settings_get_num(rs, "music_volume");

    sound_set_gain(intro_sound, gain);
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

    if (m->cmd.status && scene->exit_timeout >= 0) {
        if (!scene->exit_timeout--)
            gl_request_exit();
    }

    return 0;
}

static struct option long_options[] = {
    { "autopilot",  no_argument,        0, 'A' },
    { "fullscreen", no_argument,        0, 'F' },
    { "exitafter",  required_argument,  0, 'e' },
    { "restart",    no_argument,        0, 'R' },
    { "aoe",        no_argument,        0, 'E' },
    { "server",     required_argument,  0, 'S'},
    {}
};

static const char short_options[] = "Ae:REFS:";

int main(int argc, char **argv, char **envp)
{
    struct clap_config cfg = {
        .debug  = 1,
    };
    struct networking_config ncfg = {
        .server_ip     = CONFIG_SERVER_IP,
        .server_port   = 21044,
        .server_wsport = 21045,
    };
    int c, option_index;
    unsigned int do_restart = 0, fullscreen = 0;
    //struct lib_handle *lh;

    /*
     * Resize callback will call into projmx_update(), which depends
     * on projection matrix being allocated.
     */
    scene_init(&scene);

    for (;;) {
        c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'A':
            scene.autopilot = 1;
            break;
        case 'e':
            scene.exit_timeout = atoi(optarg);
            break;
        case 'R':
            do_restart++;
            break;
        case 'F':
            fullscreen++;
            break;
        case 'E':
            abort_on_error++;
            break;
        case 'S':
            ncfg.server_ip = optarg;
            break;
        default:
            fprintf(stderr, "invalid option %x\n", c);
            exit(EXIT_FAILURE);
        }
    }

    if (do_restart)
        cfg.quiet = 1;
    clap_init(&cfg, argc, argv, envp);

    networking_init(&ncfg, CLIENT);
    if (do_restart) {
        networking_poll();
        networking_poll();
        networking_broadcast_restart();
        networking_poll();
        networking_done();
        clap_done(0);
        return EXIT_SUCCESS;
    }

    print_each_class();
    gl_init("One Hand Clap", 1280, 720, renderFrame, &scene, resize_cb);
    (void)input_init(); /* XXX: error handling */
    //font_init();
    font_init();
    sound_init();
    phys_init();
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
    lib_request_shaders("contrast", &scene.prog);
    lib_request_shaders("hblur", &scene.prog);
    lib_request_shaders("vblur", &scene.prog);
    lib_request_shaders("model", &scene.prog);
    //lib_request_shaders("terrain", &scene);
    //lib_request_shaders("ui", &scene);

    /*
     * XXX: needs to be in the scene code, but can't be called before
     * GL is initialized
     */
    // scene.terrain = terrain_init_circular_maze(&scene, 0.0, 0.0, 0.0, 300, 32, 8);
    scene.terrain = terrain_init_square_landscape(&scene, -100.0, 0.0, -100.0, 200, 256);
    fuzzer_input_init();

    if (fullscreen)
        gl_enter_fullscreen();

    scene_camera_add(&scene);
    scene.camera = &scene.cameras[0];
    // scene_camera_add(&scene);

    scene_load(&scene, "scene.json");
    gl_get_sizes(&scene.width, &scene.height);

    ui_init(&ui, scene.width, scene.height);

    blur_pl = pipeline_new(&scene);
    pipeline_add_pass(blur_pl, "vblur");
    pipeline_add_pass(blur_pl, "hblur");
    pipeline_add_pass(blur_pl, "contrast");
    main_pl = pipeline_new(&scene);
    pipeline_add_pass(main_pl, "contrast");
    pipeline_add_pass(main_pl, "contrast");
    // fbo_update(scene.width, scene.height);

    scene.lin_speed = 1.0;
    scene.ang_speed = 45.0;

    scene.limbo_height = -70.0;
    scene_cameras_calc(&scene);

    scene.light.pos[0] = 50.0;
    scene.light.pos[1] = 50.0;
    scene.light.pos[2] = 50.0;
// #ifdef CONFIG_BROWSER
//     EM_ASM(
//         function q() {ccall("renderFrame"); setTimeout(q, 16); }
//         q();
//     );
// #else
    gl_main_loop();
// #endif

    dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    ref_put(blur_pl);
    ref_put(main_pl);
    ui_done(&ui);
    scene_done(&scene);
    phys_done();
    settings_done(settings);
    sound_done();
    //gl_done();
    clap_done(0);
#endif

    return EXIT_SUCCESS;
}
