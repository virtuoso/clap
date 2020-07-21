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
#include "messagebus.h"
#include "librarian.h"
#include "matrix.h"
#include "model.h"
#include "shader.h"
#include "terrain.h"
#include "ui.h"
#include "scene.h"
#include "sound.h"

/* XXX just note for the future */

struct scene scene; /* XXX */
struct ui ui;

EMSCRIPTEN_KEEPALIVE void renderFrame(void *data)
{
    struct scene *s = data; /* XXX */
    struct timespec     ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    scene_update(s);
    ui_update(&ui);
    /* XXX: this actually goes to ->update() */
    scene_camera_calc(s);

    /* Can't touch this */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.2f, 0.2f, 0.6f, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    models_render(&s->txmodels, &s->light, s->view_mx, s->inv_view_mx, s->proj_mx, s->focus);
    s->proj_updated = 0;
    models_render(&ui.txmodels, NULL, NULL, NULL, NULL, NULL);

    if (ts.tv_sec != s->ts.tv_sec) {
        struct message m;
        if (s->frames) {
            trace("FPS: %d\n", s->frames);
            s->FPS = s->frames;
        }
        if (s->exit_timeout >= 0) {
            if (!s->exit_timeout)
                gl_request_exit();
            else
                s->exit_timeout--;
        }

        memset(&m, 0, sizeof(m));
        m.type = MT_COMMAND;
        m.cmd.status = 1;
        m.cmd.fps = s->FPS;
        m.cmd.sys_seconds = ts.tv_sec;
        message_send(&m);
        s->frames = 0;
        s->ts.tv_sec = ts.tv_sec;
    }

    s->frames++;
    s->frames_total++;
    gl_swap_buffers();
    sound_play();
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

void resize_cb(int width, int height)
{
    ui.width = scene.width = width;
    ui.height = scene.height = height;
    scene.aspect = (float)width / (float)height;
    trace("resizing to %dx%d\n", width, height);
    glViewport(0, 0, scene.width, scene.height);
    projmx_update(&scene);
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

int font_init(void);

static struct option long_options[] = {
    { "autopilot",  no_argument,        0, 'A' },
    { "exitafter",  required_argument,  0, 'e' },
};

static const char short_options[] = "Ae:";

int main(int argc, char **argv)
{
    struct clap_config cfg = {
        .debug  = 1,
    };
    int c, option_index;
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
        default:
            fprintf(stderr, "invalid option %x\n", c);
            exit(EXIT_FAILURE);
        }
    }

#ifdef CONFIG_BROWSER
    scene.autopilot = 1;
#endif
    gl_init("One Hand Clap", 1280, 720, renderFrame, &scene, resize_cb);
    //font_init();
    clap_init(&cfg);
    font_init();
    sound_init();

    /* Before models are created */
    lib_request_shaders("model", &scene.prog);
    //lib_request_shaders("terrain", &scene);
    //lib_request_shaders("ui", &scene);

    terrain_init(&scene, 0.0, 128);

    //gl_enter_fullscreen();

    scene_load(&scene, "scene.json");
    gl_get_sizes(&scene.width, &scene.height);
    ui_init(&ui, scene.width, scene.height);

    scene.camera.pos[0] = 0.0;
    scene.camera.pos[1] = 1.0;
    scene.camera.pos[2] = 0.0;
    scene.camera.moved++;
    scene_camera_calc(&scene);

    scene.light.pos[0] = 50.0;
    scene.light.pos[1] = 50.0;
    scene.light.pos[2] = 50.0;
    scene.light.color[0] = 1.0;
    scene.light.color[1] = 1.0;
    scene.light.color[2] = 1.0;
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
    ui_done(&ui);
    scene_done(&scene);
    //gl_done();
    clap_done(0);
#endif

    return EXIT_SUCCESS;
}
