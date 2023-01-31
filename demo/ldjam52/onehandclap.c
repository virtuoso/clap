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
#include "ca3d.h"
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
#include "primitives.h"
#include "networking.h"
#include "settings.h"
#include "ui-debug.h"
#include "game.h"

/* XXX just note for the future */
struct settings *settings;
struct sound *intro_sound;
struct scene scene; /* XXX */
struct ui ui;
struct fbo *fbo;
struct game_state game_state;

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

void debug_draw_line(vec3 a, vec3 b, mat4x4 *rot)
{
    (void)__debug_draw_line(&scene, a, b, rot);
}

static void debug_draw_clearout(void)
{
    while (!list_empty(&scene.debug_draws)) {
        struct debug_draw *dd = list_first_entry(&scene.debug_draws, struct debug_draw, entry);

        list_del(&dd->entry);
        ref_put(dd);
    }
}

struct pipeline *main_pl, *blur_pl;

EMSCRIPTEN_KEEPALIVE void renderFrame(void *data)
{
    struct timespec ts_start, ts_delta;
    struct scene *s = data; /* XXX */
    unsigned long count, frame_count;
    float y0, y1, y2;
    dReal by;

    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    timespec_diff(&s->fps.ts_prev, &ts_start, &ts_delta);
#ifndef CONFIG_BROWSER
    if (ts_delta.tv_nsec < 1000000000 / gl_refresh_rate())
        return;
#endif
    clap_fps_calc(&s->fps);
    frame_count = max((unsigned long)gl_refresh_rate() / s->fps.fps_fine, 1);
    PROF_FIRST(start);

    fuzzer_input_step();

    game_update(&game_state, ts_start, ui.modal);

    scene.ts = ts_start;

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
    for (count = 0; count < frame_count; count++)
        phys_step(1);

    PROF_STEP(phys, start);

#ifndef CONFIG_FINAL
    networking_poll();
#endif
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
#ifndef CONFIG_GLES
    glEnable(GL_MULTISAMPLE);
#endif
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
    debug_draw_clearout();
}

#define FOV to_radians(70.0)

static void projmx_update(struct scene *s)
{
    struct matrix4f *m = s->proj_mx;
    float y_scale = (1 / tan(FOV / 2)) * s->aspect;
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

/*
 * XXX: this should be a message
 * cmd.resize
 */
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

static void cube_remove(struct entity3d *e, void *data)
{
    struct list *list = data;
    if (!strncmp(entity_name(e), "cubity.", 7)) {
        list_del(&e->entry);
        list_append(list, &e->entry);
    }
}

struct entity {
    int item;
    struct entity3d *entity;
};

struct cube_data {
    struct scene *s;
    struct xyzarray *xyz;
    darray(struct entity, entities);
    int ca;
    int steps;
    float side;
    bool inv;
    bool make;
    bool prune;
} cube_data;

static void cd_kill_entities(struct cube_data *cd)
{
    struct entity *ent;
    int i;

    darray_for_each(ent, &cd->entities)
        if (ent->item >= 0)
            game_item_delete_idx(&game_state, ent->item);
        else if (ent->entity)
            ref_put(ent->entity);

    darray_clearout(&cd->entities.da);
}

static struct entity3d *cd_add_entity(struct cube_data *cd, struct model3dtx *txm, float x, float y, float z, float ry)
{
    struct entity3d *e = entity3d_new(txm);
    struct entity *ent;

    CHECK(ent = darray_add(&cd->entities.da));
    e->dx = x;
    e->dy = y;
    e->dz = z;
    e->ry = ry;
    e->scale = 1;
    e->visible = 1;
    model3dtx_add_entity(txm, e);
    ent->entity = e;
    ent->item = -1;

    return e;
}

static void cd_item_kill(struct game_state *g, struct game_item *item)
{
    int idx = (long)item->priv;
    struct entity *ent = &cube_data.entities.x[idx];

    ent->item = -1;
    ref_put(item->entity);
}

static struct entity3d *cd_add_item(struct cube_data *cd, struct model3dtx *txm, float x, float y, float z, float ry)
{
    struct entity *ent;
    struct game_item *item;

    item = game_item_new(&game_state, GAME_ITEM_APPLE, txm);
    item->entity->dx = x;
    item->entity->dy = y;
    item->entity->dz = z;
    item->entity->ry = ry;
    item->entity->scale = 1;
    item->entity->visible = 1;
    item->age_limit = INFINITY;
    item->interact = game_item_collect;
    item->kill = cd_item_kill;
    CHECK(ent = darray_add(&cd->entities.da));
    item->priv = (void *)((long)cd->entities.da.nr_el - 1);
    ent->item = game_item_find_idx(&game_state, item);

    return item->entity;
}

#define CUBE_SIDE 16
static void cube_geom(struct scene *s, struct cube_data *cd, float x, float y, float z, float side)
{
    struct mesh **mesh;
    struct model3d *model;
    struct model3dtx **txm, *objtxm, *ramptxm, *tentacletxm;
    struct entity3d **entity, *e, *iter;
    struct shader_prog *prog = shader_prog_find(s->prog, "model"); /* XXX */
    float *vx, *norm, *tx;
    unsigned short *idx;
    int nr_cubes, nr_vx, nr_meshes, cubes_per_mesh;
    int cx, cy, cz, cc, cm;
    int entities = 0;
    DECLARE_LIST(list);

    mq_for_each(&s->mq, cube_remove, &list);

    /* XXX: find apple model */
    list_for_each_entry(objtxm, &s->mq.txmodels, entry)
        if (!strcmp(objtxm->model->name, "apple"))
            break;

    list_for_each_entry(ramptxm, &s->mq.txmodels, entry)
        if (!strcmp(ramptxm->model->name, "ramp"))
            break;

    list_for_each_entry(tentacletxm, &s->mq.txmodels, entry)
        if (!strcmp(tentacletxm->model->name, "tentacle"))
            break;

    list_for_each_entry_iter(e, iter, &list, entry)
        ref_put(e);

    cd_kill_entities(cd);

    if (cd->make || !cd->xyz) {
        free(cd->xyz);
        cd->xyz = ca3d_make(CUBE_SIDE, CUBE_SIDE, 8);
        nr_cubes = xyzarray_count(cd->xyz);
        cd->make = false;
        ca3d_run(cd->xyz, cd->ca, cd->steps);
    } else {
        ca3d_run(cd->xyz, cd->ca, 1);
    }

    /* a hole at the top */
    for (cx = -1; cx < 2; cx++)
        for (cy = -1; cy < 2; cy++)
            xyzarray_setat(cd->xyz, cd->xyz->dim[0] / 2 + cx, cd->xyz->dim[1] / 2 + cy, cd->xyz->dim[2] - 1, !!cd->inv);

    if (cd->prune)
        ca3d_prune(cd->xyz);
    nr_cubes = xyzarray_count(cd->xyz);
    if (!nr_cubes)
        return;

    if (cd->inv)
        nr_cubes = cd->xyz->dim[0] * cd->xyz->dim[1] * cd->xyz->dim[2] - nr_cubes;
    nr_vx = nr_cubes * mesh_nr_vx(&cube_mesh);
    // ui_debug_printf("ca: %d steps: %d inv: %d prune: %d nr_cubes: %d nr_vx: %d",
    //                 cd->ca, cd->steps, cd->inv, cd->prune, nr_cubes, nr_vx);
    /* we're limited by unsigned short for index elements */
    nr_meshes = nr_vx / 65536;
    nr_meshes += !!(nr_vx % 65536);
    cubes_per_mesh = nr_cubes / nr_meshes;
    nr_meshes += !!(nr_cubes % cubes_per_mesh);
    dbg("nr_meshes: %d\n", nr_meshes);

    CHECK(mesh = calloc(nr_meshes, sizeof(*mesh)));
    CHECK(txm = calloc(nr_meshes, sizeof(*txm)));
    CHECK(entity = calloc(nr_meshes, sizeof(*entity)));
    for (cm = 0; cm < nr_meshes; cm++) {
        mesh[cm] = mesh_new("cubity");

        mesh_attr_alloc(mesh[cm], MESH_VX, cube_mesh.attr[MESH_VX].stride, cubes_per_mesh * mesh_nr_vx(&cube_mesh));
        mesh_attr_alloc(mesh[cm], MESH_NORM, cube_mesh.attr[MESH_NORM].stride, cubes_per_mesh * mesh_nr_norm(&cube_mesh));
        mesh_attr_alloc(mesh[cm], MESH_TX, cube_mesh.attr[MESH_TX].stride, cubes_per_mesh * mesh_nr_tx(&cube_mesh));
        mesh_attr_alloc(mesh[cm], MESH_IDX, cube_mesh.attr[MESH_IDX].stride, cubes_per_mesh * mesh_nr_idx(&cube_mesh));
    }

    for (cc = 0, cm = 0, cz = 0; cz < cd->xyz->dim[2]; cz++)
        for (cy = 0; cy < cd->xyz->dim[1]; cy++)
            for (cx = 0; cx < cd->xyz->dim[0]; cx++)
                if (!!xyzarray_getat(cd->xyz, cx, cy, cz) == !cd->inv) {
                    mesh_push_mesh(mesh[cm], &cube_mesh,
                                   side * cx + x, side * cz + y, side * cy + z, side);
                    cc++;
                    if (cc == cubes_per_mesh) {
                        cm++;
                        cc = 0;
                    }
                } else {
                    struct entity3d *e;
                    bool skip = false;

                    if (ca3d_neighbors_vn1(cd->xyz, cx, cy, cz) == 5 &&
                        !!xyzarray_getat(cd->xyz, cx, cy, cz - 1) == !cd->inv) {
                        cd_add_item(cd, objtxm,
                                    side * cx + x + side / 2,
                                    side * cz + y + side / 4 - drand48() / 2,
                                    side * cy + z + side / 2, 0);
                        skip = true;
                    }
                    if (!!xyzarray_getat(cd->xyz, cx, cy, cz + 1) != !cd->inv &&
                        !!xyzarray_getat(cd->xyz, cx, cy, cz - 1) == !cd->inv) {
                        if (!!xyzarray_getat(cd->xyz, cx + 1, cy, cz + 1) != !cd->inv &&
                            !!xyzarray_getat(cd->xyz, cx + 1, cy, cz) == !cd->inv) {
                            e = cd_add_entity(cd, ramptxm,
                                              side * cx + x + side / 2,
                                              side * cz + y,
                                              side * cy + z + side / 2, to_radians(-90));
                            entity3d_add_physics(e, dInfinity, dTriMeshClass, PHYS_GEOM, 0, 0, 0);
                            entities++;
                        } else if (!!xyzarray_getat(cd->xyz, cx - 1, cy, cz + 1) != !cd->inv &&
                            !!xyzarray_getat(cd->xyz, cx - 1, cy, cz) == !cd->inv) {
                            e = cd_add_entity(cd, ramptxm,
                                              side * cx + x + side / 2,
                                              side * cz + y,
                                              side * cy + z + side / 2, to_radians(90));
                            entity3d_add_physics(e, dInfinity, dTriMeshClass, PHYS_GEOM, 0, 0, 0);
                        } else if (!!xyzarray_getat(cd->xyz, cx, cy - 1, cz + 1) != !cd->inv &&
                            !!xyzarray_getat(cd->xyz, cx, cy - 1, cz) == !cd->inv) {
                            e = cd_add_entity(cd, ramptxm,
                                              side * cx + x + side / 2,
                                              side * cz + y,
                                              side * cy + z + side / 2, 0);
                            entity3d_add_physics(e, dInfinity, dTriMeshClass, PHYS_GEOM, 0, 0, 0);
                        } else if (!!xyzarray_getat(cd->xyz, cx, cy + 1, cz + 1) != !cd->inv &&
                            !!xyzarray_getat(cd->xyz, cx, cy + 1, cz) == !cd->inv) {
                            e = cd_add_entity(cd, ramptxm,
                                              side * cx + x + side / 2,
                                              side * cz + y,
                                              side * cy + z + side / 2, to_radians(180));
                            entity3d_add_physics(e, dInfinity, dTriMeshClass, PHYS_GEOM, 0, 0, 0);
                        } else if (!skip && drand48() > 0.9) {
                            e = cd_add_entity(cd, tentacletxm,
                                              side * cx + x + side / 2,
                                              side * cz + y,
                                              side * cy + z + side / 2, drand48() * 2 * M_PI);
                        }
                    }
                }
    for (cm = 0; cm < nr_meshes; cm++) {
        // mesh_optimize(mesh[cm]);
        model = model3d_new_from_mesh("cubity", prog, mesh[cm]);
        model3d_set_name(model, "cubity.%d", cm);
        model->collision_vx = mesh_vx(mesh[cm]);
        model->collision_vxsz = mesh_vx_sz(mesh[cm]);
        model->collision_idx = mesh_idx(mesh[cm]);
        model->collision_idxsz = mesh_idx_sz(mesh[cm]);

        txm[cm] = model3dtx_new(ref_pass(model), "purple wall seamless.png");
        scene_add_model(s, txm[cm]);
        entity[cm] = entity3d_new(txm[cm]);
        entity[cm]->visible = 1;
        entity[cm]->update  = NULL;
        entity[cm]->scale = 1;
        entity3d_reset(entity[cm]);
        model3dtx_add_entity(txm[cm], entity[cm]);
        entity3d_add_physics(entity[cm], 0, dTriMeshClass, PHYS_GEOM, 0, 0, 0);
    }
    free(mesh);
    free(txm);
    free(entity);
    ref_put(prog); /* matches shader_prog_find() above */
    /* drop the character on top of the structure */
    entity3d_position(s->control->entity,
                      x + cd->xyz->dim[0] * cd->side / 2,
                      y + cd->xyz->dim[2] * cd->side + 4,
                      z + cd->xyz->dim[1] * cd->side / 2);
    pocket_total_set(game_state.ui, 0, entities);
}

static int cube_input(struct message *m, void *data)
{
    struct message_input *mi = &m->input;
    struct cube_data *cd = data;
    struct scene *s = cd->s;
    bool gen = false;

    if (!mi->pad_lb)
        return 0;

    cd->side = 2;
    if (mi->trigger_l > 0.5) {
        cd->ca = 4;
        cd->steps = 4;
        cd->make = true;
        gen = true;
    } else if (mi->trigger_r > 0.5) {
        cd->ca = 7;
        cd->steps = 4;
        cd->make = true;
        gen = true;
    } else if (mi->up == 1) {
        /* LB + Up: next automaton */
        cd->ca++;
        cd->make = true;
        gen = true;
    } else if (mi->down == 1) {
        /* LB + Down: previous automaton */
        cd->ca--;
        cd->make = true;
        gen = true;
    } else if (mi->right == 1) {
        /* LB + Right: more steps */
        cd->steps++;
        cd->make = false;
        gen = true;
    } else if (mi->left == 1) {
        /* LB + Left: fewer steps */
        cd->steps--;
        cd->make = true;
        gen = true;
    } else if (mi->pad_x == 1) {
        /* LB + X: invert cubes */
        cd->inv = !cd->inv;
        cd->make = true;
        gen = true;
    } else if (mi->pad_y == 1) {
        /* LB + X: prune cubes */
        cd->prune = !cd->prune;
        cd->make = false;
        gen = true;
    }

    if (gen)
        cube_geom(s, cd, 10, CUBE_SIDE * cd->side / 2, 10, cd->side);
    return gen ? MSG_STOP : MSG_HANDLED;
}

static void mushroom_interact(struct game_state *g, struct game_item *item, struct entity3d *actor)
{
    struct cube_data *cd = &cube_data;

    game_item_collect(g, item, actor);
    dbg("start a dungeon\n");
    cd->side  = 2;
    cd->ca    = 7;
    cd->steps = 4;
    cd->make  = true;
    cube_geom(&scene, cd, 10, CUBE_SIDE * cd->side / 2, 10, cd->side);
}

static void spawn_mushrooms(struct game_state *g)
{
    struct game_item *item;
    int nr_items = 30, i;

    for (i = 0; i < nr_items; i++) {
        item = game_item_spawn(g, GAME_ITEM_MUSHROOM);
        item->interact = mushroom_interact;
        item->age_limit = INFINITY;
    }
}

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
    { "aoe",        no_argument,        0, 'E' },
    { "server",     required_argument,  0, 'S'},
    {}
};

static const char short_options[] = "Ae:EFS:";

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
    unsigned int fullscreen = 0;
    struct render_pass *pass;
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
        case 'F':
            fullscreen++;
            break;
#ifndef CONFIG_FINAL
        case 'A':
            scene.autopilot = 1;
            break;
        case 'e':
            scene.exit_timeout = atoi(optarg);
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

    clap_init(&cfg, argc, argv, envp);

#ifndef CONFIG_FINAL
    networking_init(&ncfg, CLIENT);
#endif

    print_each_class();
    gl_init("One Hand Clap", 1280, 720, renderFrame, &scene, resize_cb);
    (void)input_init(); /* XXX: error handling */
    //font_init();
    font_init();
    sound_init();
    phys_init();
    phys->ground_contact = ohc_ground_contact;

    cube_data.s = &scene;
    cube_data.make = true;
    darray_init(&cube_data.entities);
    subscribe(MT_INPUT, cube_input, &cube_data);

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
    lib_request_shaders("debug", &scene.prog);
    lib_request_shaders("terrain", &scene.prog);
    lib_request_shaders("model", &scene.prog);
    //lib_request_shaders("ui", &scene);

    /*
     * XXX: needs to be in the scene code, but can't be called before
     * GL is initialized
     */
    // scene.terrain = terrain_init_circular_maze(&scene, 0.0, 0.0, 0.0, 300, 32, 8);
    scene.terrain = terrain_init_square_landscape(&scene, -40.0, 0.0, -40.0, 80, 256);
    fuzzer_input_init();

    if (fullscreen)
        gl_enter_fullscreen();

    scene_camera_add(&scene);
    scene.camera = &scene.cameras[0];
    // scene_camera_add(&scene);

    scene_load(&scene, "scene.json");

    game_init(&scene, &ui); // this must happen after scene_load, because we need the trees.
    spawn_mushrooms(&game_state);
    subscribe(MT_INPUT, handle_game_input, NULL);

    gl_get_sizes(&scene.width, &scene.height);

    ui_init(&ui, scene.width, scene.height);

    blur_pl = pipeline_new(&scene);
    pass = pipeline_add_pass(blur_pl, NULL, NULL, true);
    pass = pipeline_add_pass(blur_pl, pass, "vblur", false);
    pass = pipeline_add_pass(blur_pl, pass, "hblur", false);
    main_pl = pipeline_new(&scene);
    pass = pipeline_add_pass(main_pl, NULL, NULL, true);
    pass = pipeline_add_pass(main_pl, pass, "contrast", false);
    // pass = pipeline_add_pass(main_pl, pass, "contrast", false);

    scene.lin_speed = 2.0;
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
