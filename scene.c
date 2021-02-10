#include "messagebus.h"
#include "physics.h"
#include "shader.h"
#include "terrain.h"
#include "model.h"
#include "scene.h"
#include "sound.h"
#include "json.h"

struct sound *click;

static void scene_camera_autopilot(struct scene *s)
{
    s->camera.pos[1] = s->auto_yoffset + 2.0;

    s->camera.motion[2] = -s->lin_speed;
    s->camera.motion[0] = 0;
    s->camera.yaw_turn  = -s->ang_speed / 5;
    s->camera.moved++;
}

static struct model3dtx *scene_nonempty_txm_next(struct scene *s, struct model3dtx *txm, bool fwd)
{
    struct model3dtx *first_txm = list_first_entry(&s->txmodels, struct model3dtx, entry);
    struct model3dtx *last_txm = list_last_entry(&s->txmodels, struct model3dtx, entry);
    struct model3dtx *next_txm;

    if (list_empty(&s->txmodels))
        return NULL;

    if (!txm)
        txm = fwd ? last_txm : first_txm;
    next_txm = txm;

    do {
        if (next_txm == first_txm)
            next_txm = last_txm;
        else if (next_txm == last_txm)
            next_txm = first_txm;
        else
            next_txm = fwd ?
                list_next_entry(next_txm, entry) :
                list_prev_entry(next_txm, entry);
    } while (list_empty(&next_txm->entities) && next_txm != txm);

    return list_empty(&next_txm->entities) ? NULL : next_txm;
}

static void scene_focus_next(struct scene *s)
{
    struct model3dtx *next_txm;

    sound_play(click);
    if (!s->focus) {
        next_txm = scene_nonempty_txm_next(s, NULL, true);
        /* nothing to focus on */
        if (!next_txm)
            return;

        goto first_entry;
    }

    if (s->focus != list_last_entry(&s->focus->txmodel->entities, struct entity3d, entry)) {
        s->focus = list_next_entry(s->focus, entry);
        return;
    }

    next_txm = scene_nonempty_txm_next(s, s->focus->txmodel, true);
    if (!next_txm)
        return;

first_entry:
    s->focus = list_first_entry(&next_txm->entities, struct entity3d, entry);
}

static void scene_focus_prev(struct scene *s)
{
    struct model3dtx *next_txm;

    sound_play(click);
    if (!s->focus) {
        next_txm = scene_nonempty_txm_next(s, NULL, false);
        if (!next_txm)
            return;
        goto last_entry;
    }

    if (s->focus != list_first_entry(&s->focus->txmodel->entities, struct entity3d, entry)) {
        s->focus = list_prev_entry(s->focus, entry);
        return;
    }

    next_txm = scene_nonempty_txm_next(s, s->focus->txmodel, false);
    if (!next_txm)
        return;
last_entry:
    s->focus = list_last_entry(&next_txm->entities, struct entity3d, entry);
}

static void scene_focus_cancel(struct scene *s)
{
    s->focus = NULL;
}

void scene_camera_calc(struct scene *s)
{
    float scalev[3], yawcos, yawsin;
    vec3 inc;
    int i;

    if (!s->fps.fps_fine)
        return;
    if (s->autopilot)
        scene_camera_autopilot(s);
    if (!s->camera.moved)
        return;

    for (i = 0; i < 3; i++)
        scalev[i] = s->camera.zoom ? 3.0 : 1.0;

    s->camera.pitch += s->camera.pitch_turn / (float)s->fps.fps_fine;
    s->camera.pitch = clampf(s->camera.pitch, -90, 90);
    s->camera.yaw += s->camera.yaw_turn / (float)s->fps.fps_fine;
    if (s->camera.yaw > 180)
        s->camera.yaw -= 360;
    else if (s->camera.yaw <= -180)
        s->camera.yaw += 360;
    s->camera.pitch_turn = 0;
    s->camera.yaw_turn = 0;

    yawcos = cos(to_radians(s->camera.yaw));
    yawsin = sin(to_radians(s->camera.yaw));
    s->camera.angle[0] = yawsin;
    s->camera.angle[2] = yawcos;

    if (vec3_len(s->camera.motion) != 0) {
        float x1, y1;

        x1 = yawcos * s->camera.motion[0] - yawsin * s->camera.motion[2];
        y1 = yawsin * s->camera.motion[0] + yawcos * s->camera.motion[2];
        s->camera.motion[0] = x1;
        s->camera.motion[2] = y1;
    }

    vec3_scale(inc, s->camera.motion, 1.f / (float)s->fps.fps_fine);
    vec3_add(s->camera.pos, s->camera.pos, inc);

    s->camera.motion[0] = 0;
    s->camera.motion[1] = 0;
    s->camera.motion[2] = 0;

    s->camera.moved = 0;
    trace("camera: %f/%f/%f zoom: %d\n", s->camera.pos[0], s->camera.pos[1], s->camera.pos[2], s->camera.zoom);

    //free(s->view_mx);
    //s->view_mx = transmx_new(negpos, 0.0, 0.0, 0.0, 1.0);
    mat4x4_identity(s->view_mx->m);
    mat4x4_rotate_X(s->view_mx->m, s->view_mx->m, to_radians(s->camera.pitch));
    mat4x4_rotate_Y(s->view_mx->m, s->view_mx->m, to_radians(s->camera.yaw));
    mat4x4_scale_aniso(s->view_mx->m, s->view_mx->m, scalev[0], scalev[1], scalev[2]);
    //mat4x4_scale(s->view_mx->m, scalev, 1.0);
    mat4x4_translate_in_place(s->view_mx->m, -s->camera.pos[0], -s->camera.pos[1], -s->camera.pos[2]);

    mat4x4_invert(s->inv_view_mx->m, s->view_mx->m);
    if (!(s->frames_total & 0xf))
        gl_title("One Hand Clap @%d FPS camera [%f,%f,%f] [%f/%f]", s->fps.fps_coarse,
                 s->camera.pos[0], s->camera.pos[1], s->camera.pos[2],
                 s->camera.pitch, s->camera.yaw);
}

static int scene_handle_command(struct message *m, void *data)
{
    struct scene *s = data;

    if (m->cmd.toggle_autopilot)
        s->autopilot ^= 1;

    return 0;
}

static int scene_handle_input(struct message *m, void *data)
{
    struct scene *s = data;
    float delta_x = 0, delta_z = 0;
    float lin_speed = s->lin_speed;

    /*trace("input event %d/%d/%d/%d %f/%f/%f exit %d\n",
        m->input.left, m->input.right, m->input.up, m->input.down,
        m->input.delta_x, m->input.delta_y, m->input.delta_z,
        m->input.exit);*/
    if (m->input.exit)
        gl_request_exit();
    if (m->input.resize)
        gl_resize(m->input.x, m->input.y);
    if (m->input.autopilot)
        s->autopilot ^= 1;
    if (m->input.focus_next)
        scene_focus_next(s);
    if (m->input.focus_prev)
        scene_focus_prev(s);
    if (m->input.focus_cancel)
        scene_focus_cancel(s);
    if (m->input.fullscreen) {
        if (s->fullscreen)
            gl_leave_fullscreen();
        else
            gl_enter_fullscreen();
        s->fullscreen ^= 1;
        trace("fullscreen: %d\n", s->fullscreen);
    }

    if (m->input.verboser) {
        struct message m;

        msg("toggle noise\n");
        /* XXX: factor out */
        memset(&m, 0, sizeof(m));
        m.type = MT_COMMAND;
        m.cmd.toggle_noise = 1;
        message_send(&m);
    }

    if (m->input.trigger_r)
        lin_speed *= (m->input.trigger_r + 1) * 10;
    else if (m->input.pad_rt)
        lin_speed *= 10;

    /* Use data from joystick sticks when available */
    if (m->input.delta_lx || m->input.delta_ly) {
        delta_x = m->input.delta_lx * lin_speed;
        delta_z = m->input.delta_ly * lin_speed;
    } else {
        if (m->input.right) {
            if (s->focus)
                entity3d_move(s->focus, 0.1, 0, 0);
            else
                delta_x = lin_speed;
        }
        if (m->input.left) {
            if (s->focus)
                entity3d_move(s->focus, -0.1, 0, 0);
            else
                delta_x = -lin_speed;
        }
        if (m->input.up) {
            if (s->focus)
                entity3d_move(s->focus, 0, 0, 0.1);
            else
                delta_z = lin_speed;
        }
        if (m->input.down) {
            if (s->focus)
                entity3d_move(s->focus, 0, 0, -0.1);
            else
                delta_z = -lin_speed;
        }
    }

    if (m->input.pitch_up && s->camera.pitch < 90)
        s->camera.pitch_turn = s->ang_speed;
    if (m->input.pitch_down && s->camera.pitch > -90)
        s->camera.pitch_turn = -s->ang_speed;

    if (m->input.delta_rx) {
        s->camera.yaw_turn = s->ang_speed * m->input.delta_rx;
    } else {
        if (m->input.yaw_right)
            s->camera.yaw_turn = s->ang_speed;
        if (m->input.yaw_left)
            s->camera.yaw_turn = -s->ang_speed;
    }

    s->camera.motion[0] = delta_x;
    s->camera.motion[1] = 0.0;
    s->camera.motion[2] = delta_z;

    s->camera.zoom = !!(m->input.zoom);
    s->camera.pos[1] -= (m->input.delta_ry / 10.0) * lin_speed;
    s->camera.moved++;

    return 0;
}

int scene_add_model(struct scene *s, struct model3dtx *txm)
{
    list_append(&s->txmodels, &txm->entry);
    return 0;
}

static void scene_light_update(struct scene *scene)
{
    float day[3]        = { 1.0, 1.0, 1.0 };
    float night[3]      = { 0.3, 0.3, 0.4 };

    scene->light.pos[0] = 500.0 * cos(to_radians((float)scene->frames_total / 4.0));
    scene->light.pos[1] = 500.0 * sin(to_radians((float)scene->frames_total / 4.0));
    scene->light.pos[2] = 0.0;
    if (scene->light.pos[1] < 0.0) {
        scene->light.pos[1] = -scene->light.pos[1];
        memcpy(scene->light.color, night, sizeof(night));
    } else {
        memcpy(scene->light.color, day, sizeof(day));
    }
}

void scene_update(struct scene *scene)
{
    struct model3dtx *txm;
    struct entity3d *ent;

    scene_light_update(scene);

    list_for_each_entry(txm, &scene->txmodels, entry) {
        list_for_each_entry(ent, &txm->entities, entry) {
            entity3d_update(ent, scene);
        }
    }
}

int scene_init(struct scene *scene)
{
    memset(scene, 0, sizeof(*scene));
    scene->proj_mx      = mx_new();
    scene->view_mx      = mx_new();
    scene->inv_view_mx  = mx_new();
    scene->exit_timeout = -1;
    scene->auto_yoffset = 4.0;
    list_init(&scene->txmodels);

    subscribe(MT_INPUT, scene_handle_input, scene);
    subscribe(MT_COMMAND, scene_handle_command, scene);

    return 0;
}

struct scene_config {
    char            name[256];
    struct model    *model;
    unsigned long   nr_model;
};

struct scene_model_queue {
    JsonNode          *entities;
    unsigned int      loaded;
    struct model3d    *model;
    struct model3dtx  *txm;
    struct scene      *scene;
};

static int model_new_from_json(struct scene *scene, JsonNode *node)
{
    double mass = 1.0, bounce = 0.0, bounce_vel = dInfinity, geom_off = 1.0, geom_radius = 1.0;
    char *name = NULL, *obj = NULL, *binvec = NULL, *tex = NULL;
    JsonNode *p, *ent = NULL, *phys = NULL;
    enum geom_type geom = GEOM_SPHERE;
    struct lib_handle *libh;
    struct model3dtx *txm;

    if (node->tag != JSON_OBJECT) {
        dbg("json: model is not an object\n");
        return -1;
    }

    for (p = node->children.head; p; p = p->next) {
        if (p->tag == JSON_STRING && !strcmp(p->key, "name"))
            name = p->string_;
        else if (p->tag == JSON_STRING && !strcmp(p->key, "obj"))
            obj = p->string_;
        else if (p->tag == JSON_STRING && !strcmp(p->key, "binvec"))
            binvec = p->string_;
        else if (p->tag == JSON_STRING && !strcmp(p->key, "texture"))
            tex = p->string_;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "physics"))
            phys = p;
        else if (p->tag == JSON_ARRAY && !strcmp(p->key, "entity"))
            ent = p->children.head;
    }

    if (!name || (!!obj == !!binvec) || !tex) {
        dbg("json: name '%s' obj '%s' binvec '%s' tex '%s'\n",
            name, obj, binvec, tex);
        return -1;
    }
    
    if (obj) {
        libh = lib_request_obj(obj, scene);
    } else {
        libh = lib_request_bin_vec(binvec, scene);
    }
    ref_put_last(&libh->ref);

    model3d_set_name(scene->_model, name);
    txm = model3dtx_new(scene->_model, tex);
    ref_put(&scene->_model->ref);
    scene_add_model(scene, txm);

    if (phys) {
        for (p = phys->children.head; p; p = p->next) {
            if (p->tag == JSON_NUMBER && !strcmp(p->key, "bounce"))
                bounce = p->number_;
            else if (p->tag == JSON_NUMBER && !strcmp(p->key, "bounce_vel"))
                bounce_vel = p->number_;
            else if (p->tag == JSON_NUMBER && !strcmp(p->key, "mass"))
                mass = p->number_;
            else if (p->tag == JSON_NUMBER && !strcmp(p->key, "zoffset"))
                geom_off = p->number_;
            else if (p->tag == JSON_NUMBER && !strcmp(p->key, "radius"))
                geom_radius = p->number_;
            else if (p->tag == JSON_STRING && !strcmp(p->key, "geom")) {
                if (!strcmp(p->string_, "trimesh"))
                    geom = GEOM_TRIMESH;
                else if (!strcmp(p->string_, "sphere"))
                    geom = GEOM_SPHERE;
            }
        }
    }

    if (ent) {
        for (; ent; ent = ent->next) {
            struct entity3d *e;
            JsonNode *pos;

            if (ent->tag != JSON_ARRAY)
                continue; /* XXX: in fact, no */

            e = entity3d_new(txm);
            pos = ent->children.head;
            if (pos->tag != JSON_NUMBER)
                continue; /* XXX */
            e->dx = pos->number_;
            pos = pos->next;      
            if (!pos || pos->tag != JSON_NUMBER)
                continue; /* XXX */
            e->dy = pos->number_;
            pos = pos->next;
            if (!pos || pos->tag != JSON_NUMBER)
                continue; /* XXX */
            e->dz = pos->number_;
            pos = pos->next;
            if (!pos || pos->tag != JSON_NUMBER)
                continue; /* XXX */
            e->scale = pos->number_;

            mat4x4_translate_in_place(e->mx->m, e->dx, e->dy, e->dz);
            mat4x4_scale_aniso(e->mx->m, e->mx->m, e->scale, e->scale, e->scale);
            e->visible        = 1;
            model3dtx_add_entity(txm, e);

            /*
             * XXX: This kinda requires that "physics" goes before "entity"
             */
            if (phys) {
                entity3d_add_physics(e, mass, geom, geom_off, geom_radius);
                e->phys_body->bounce = bounce;
                e->phys_body->bounce_vel = bounce_vel;
            }
            trace("added '%s' entity at %f,%f,%f scale %f\n", name, e->dx, e->dy, e->dz, e->scale);
        }
    } else {
        create_entities(txm);
    }

    dbg("loaded model '%s'\n", name);

    return 0;
}

static void scene_onload(struct lib_handle *h, void *buf)
{
    struct scene *scene = buf;
    char         msg[256];
    LOCAL(JsonNode, node);
    JsonNode *p, *m;

    node = json_decode(h->buf);
    if (!node) {
        err("couldn't parse '%s'\n", h->name);
        return;
    }

    if (!json_check(node, msg)) {
        err("error parsing '%s': '%s'\n", h->name, msg);
        return;
    }

    if (node->tag != JSON_OBJECT) {
        err("parse error in '%s'\n", h->name);
        return;
    }

    for (p = node->children.head; p; p = p->next) {
        if (!strcmp(p->key, "name")) {
            if (p->tag != JSON_STRING) {
                err("parse error in '%s'\n", h->name);
                return;
            }
            scene->name = strdup(p->string_);
        } else if (!strcmp(p->key, "model")) {
            if (p->tag != JSON_ARRAY) {
                err("parse error in '%s'\n", h->name);
                return;
            }

            for (m = p->children.head; m; m = m->next) {
                model_new_from_json(scene, m);
            }
        }
    }
    dbg("loaded scene: '%s'\n", scene->name);
    ref_put(&h->ref);
}

int scene_load(struct scene *scene, const char *name)
{
    struct lib_handle *lh = lib_request(RES_ASSET, name, scene_onload, scene);

    err_on(lh->state != RES_LOADED);
    ref_put_last(&lh->ref);
    click = sound_load("stapler.ogg");

    return 0;
}

void scene_done(struct scene *scene)
{
    struct model3dtx *txmodel, *ittxm;
    struct entity3d *ent, *itent;

    terrain_done(scene->terrain);

    /*
     * Question: do higher-level objects hold the last reference to the
     * lower-level objects: txmodels on models, entities on txmodels.
     * 
     * As of this writing, it's true for the latter and false for the
     * former, which is inconsistent and will yield bugs.
     */
    list_for_each_entry_iter(txmodel, ittxm, &scene->txmodels, entry) {
    //while (!list_empty(&scene->txmodels)) {
        //txmodel = list_first_entry(&scene->txmodels, struct model3dtx, entry);
        dbg("freeing entities of '%s'\n", txmodel->model->name);
        list_for_each_entry_iter(ent, itent, &txmodel->entities, entry) {
            ref_put(&ent->ref);
        }
        ref_put_last(&txmodel->ref);
    }
}
