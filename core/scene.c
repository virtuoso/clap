// SPDX-License-Identifier: Apache-2.0
#include "messagebus.h"
#include "character.h"
#include "gltf.h"
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
    s->camera->ch->pos[1] = s->auto_yoffset + 2.0;

    s->camera->ch->motion[2] = -s->lin_speed;
    s->camera->ch->motion[0] = 0;
    s->camera->yaw_turn  = -s->ang_speed / 5;
    s->camera->ch->moved++;
}

static void scene_control_next(struct scene *s)
{
    struct character *first, *last;

    if (list_empty(&s->characters))
        return;

    first = list_first_entry(&s->characters, struct character, entry);
    last = list_last_entry(&s->characters, struct character, entry);
    if (!s->control || s->control == last)
        s->control = first;
    else
        s->control = list_next_entry(s->control, entry);

    s->control->camera = s->camera;
    s->control->moved++;
    s->camera->dist = 10;
    s->camera->ch->moved++;

    trace("scene control at: '%s'\n", entity_name(s->control->entity));
    dbg("scene control at: '%s'\n", entity_name(s->control->entity));
}

static void scene_focus_next(struct scene *s)
{
    struct model3dtx *next_txm;

    sound_play(click);
    if (!s->focus) {
        next_txm = mq_nonempty_txm_next(&s->mq, NULL, true);
        /* nothing to focus on */
        if (!next_txm)
            return;

        goto first_entry;
    }

    if (s->focus != list_last_entry(&s->focus->txmodel->entities, struct entity3d, entry)) {
        s->focus = list_next_entry(s->focus, entry);
        return;
    }

    next_txm = mq_nonempty_txm_next(&s->mq, s->focus->txmodel, true);
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
        next_txm = mq_nonempty_txm_next(&s->mq, NULL, false);
        if (!next_txm)
            return;
        goto last_entry;
    }

    if (s->focus != list_first_entry(&s->focus->txmodel->entities, struct entity3d, entry)) {
        s->focus = list_prev_entry(s->focus, entry);
        return;
    }

    next_txm = mq_nonempty_txm_next(&s->mq, s->focus->txmodel, false);
    if (!next_txm)
        return;
last_entry:
    s->focus = list_last_entry(&next_txm->entities, struct entity3d, entry);
}

static void scene_focus_cancel(struct scene *s)
{
    s->focus = NULL;
}

bool scene_camera_follows(struct scene *s, struct character *ch)
{
    return s->control == ch && ch != s->camera->ch;
}

int scene_camera_add(struct scene *s)
{
    struct model3d *m = model3d_new_cube(s->prog);
    struct model3dtx *txm = model3dtx_new(m, "transparent.png");
    struct entity3d *entity;

    ref_put(m);
    s->camera = &s->cameras[s->nr_cameras];
    s->camera->ch = character_new(txm, s);
    entity = character_entity(s->camera->ch);
    s->control   = s->camera->ch;
    model3d_set_name(m, "camera");
    model3dtx_add_entity(txm, entity);
    scene_add_model(s, entity->txmodel);
    ref_put(entity->txmodel);

    s->camera->view_mx      = mx_new();
    s->camera->inv_view_mx  = mx_new();


    s->camera->ch->pos[0] = 0.0;
    s->camera->ch->pos[1] = 3.0;
    s->camera->ch->pos[2] = -4.0 + s->nr_cameras * 2;
    s->camera->yaw        = 180;
    s->camera->ch->moved++;
    s->camera->dist = 10;

    return s->nr_cameras++;
}

static void scene_camera_calc(struct scene *s, int camera)
{
    float scalev[3];
    int i;

    if (!s->fps.fps_fine)
        return;
    if (s->autopilot)
        scene_camera_autopilot(s);
    if (!s->cameras[camera].ch->moved && s->control == s->cameras[camera].ch)
        return;

    for (i = 0; i < 3; i++)
        scalev[i] = s->cameras[camera].zoom ? 3.0 : 1.0;

    s->cameras[camera].pitch += s->cameras[camera].pitch_turn / (float)s->fps.fps_fine;
    s->cameras[camera].pitch = clampf(s->cameras[camera].pitch, -90, 90);
    s->cameras[camera].yaw += s->cameras[camera].yaw_turn / (float)s->fps.fps_fine;
        if (s->cameras[camera].yaw > 180)
            s->cameras[camera].yaw -= 360;
        else if (s->cameras[camera].yaw <= -180)
            s->cameras[camera].yaw += 360;

    /* circle the character s->control */
    if (s->control != s->cameras[camera].ch &&
        (s->cameras[camera].yaw_turn || s->cameras[camera].pitch_turn || s->control->moved || s->cameras[camera].ch->moved)) {
        struct camera *cam = &s->cameras[camera];

        // float dist           = s->cameras[camera].zoom ? 1 : 10;
        float x              = s->control->pos[0];
        float y              = s->control->pos[1] + entity3d_aabb_Y(s->control->entity) / 4 * 3;
        float z              = s->control->pos[2];
        cam->ch->pos[0] = x + cam->dist * sin(to_radians(-s->cameras[camera].yaw)) * cos(to_radians(s->cameras[camera].pitch));
        cam->ch->pos[1] = y + cam->dist * sin(to_radians(s->cameras[camera].pitch));
        cam->ch->pos[2] = z + cam->dist * cos(to_radians(-s->cameras[camera].yaw)) * cos(to_radians(s->cameras[camera].pitch));
        //s->control->moved    = 0; /* XXX */
    }

    s->cameras[camera].pitch_turn = 0;
    s->cameras[camera].yaw_turn = 0;

    s->cameras[camera].ch->moved = 0;
    trace("camera: %f/%f/%f zoom: %d\n", s->cameras[camera].ch->pos[0], s->cameras[camera].ch->pos[1], s->cameras[camera].ch->pos[2], s->cameras[camera].zoom);

    //free(s->view_mx);
    //s->view_mx = transmx_new(negpos, 0.0, 0.0, 0.0, 1.0);
    mat4x4_identity(s->cameras[camera].view_mx->m);
    mat4x4_rotate_X(s->cameras[camera].view_mx->m, s->cameras[camera].view_mx->m, to_radians(s->cameras[camera].pitch));
    mat4x4_rotate_Y(s->cameras[camera].view_mx->m, s->cameras[camera].view_mx->m, to_radians(s->cameras[camera].yaw));
    mat4x4_scale_aniso(s->cameras[camera].view_mx->m, s->cameras[camera].view_mx->m, scalev[0], scalev[1], scalev[2]);
    //mat4x4_scale(s->cameras[camera].view_mx->m, scalev, 1.0);
    mat4x4_translate_in_place(s->cameras[camera].view_mx->m, -s->cameras[camera].ch->pos[0], -s->cameras[camera].ch->pos[1], -s->cameras[camera].ch->pos[2]);

    mat4x4_invert(s->cameras[camera].inv_view_mx->m, s->cameras[camera].view_mx->m);
    if (!(s->frames_total & 0xf) && camera == 0)
        gl_title("One Hand Clap @%d FPS camera0 [%f,%f,%f] [%f/%f]", s->fps.fps_coarse,
                 s->cameras[camera].ch->pos[0], s->cameras[camera].ch->pos[1], s->cameras[camera].ch->pos[2],
                 s->cameras[camera].pitch, s->cameras[camera].yaw);
}

void scene_cameras_calc(struct scene *s)
{
    int i;

    for (i = 0; i < s->nr_cameras; i++)
        scene_camera_calc(s, i);
}

void scene_characters_move(struct scene *s)
{
    struct character *ch;

    list_for_each_entry(ch, &s->characters, entry) {
        character_move(ch, s);
    }
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
    if (m->input.tab || m->input.stick_r)
        scene_control_next(s);
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

    character_handle_input(s->control, s, m);
    //trace("## control is grounded: %d\n", character_is_grounded(s->control, s));

    return 0;
}

int scene_add_model(struct scene *s, struct model3dtx *txm)
{
    mq_add_model(&s->mq, txm);
    return 0;
}

static void scene_light_update(struct scene *scene)
{
    float day[3]        = { 1.0, 1.0, 1.0 };
    float night[3]      = { 0.3, 0.3, 0.4 };

    /* we'll have to get rid of this */
    // scene->light.pos[0] = 500.0 * cos(to_radians((float)scene->frames_total / 10.0));
    // scene->light.pos[1] = 500.0 * sin(to_radians((float)scene->frames_total / 10.0));
    scene->light.pos[0] = 0.0;
    scene->light.pos[1] = 500.0;
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
    struct entity3d  *ent;

    scene_light_update(scene);

    mq_update(&scene->mq);
}

int scene_init(struct scene *scene)
{
    memset(scene, 0, sizeof(*scene));
    scene->proj_mx      = mx_new();
    scene->exit_timeout = -1;
    scene->auto_yoffset = 4.0;
    mq_init(&scene->mq, scene);
    list_init(&scene->characters);

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
    double mass = 1.0, bounce = 0.0, bounce_vel = dInfinity, geom_off = 0.0, geom_radius = 1.0, geom_length = 1.0;
    char *name = NULL, *obj = NULL, *binvec = NULL, *gltf = NULL, *tex = NULL;
    bool terrain_clamp = false, cull_face = true, alpha_blend = false;
    JsonNode *p, *ent = NULL, *ch = NULL, *phys = NULL;
    int class = dSphereClass, collision = -1, ptype = PHYS_BODY;
    struct gltf_data *gd = NULL;
    struct lib_handle *libh;
    struct model3dtx  *txm;

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
        else if (p->tag == JSON_STRING && !strcmp(p->key, "gltf"))
            gltf = p->string_;
        else if (p->tag == JSON_STRING && !strcmp(p->key, "texture"))
            tex = p->string_;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "physics"))
            phys = p;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "terrain_clamp"))
            terrain_clamp = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "cull_face"))
            cull_face = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "alpha_blend"))
            alpha_blend = p->bool_;
        else if (p->tag == JSON_ARRAY && !strcmp(p->key, "entity"))
            ent = p->children.head;
        else if (p->tag == JSON_ARRAY && !strcmp(p->key, "character"))
            ch = p->children.head;
    }

    if (!name || (!obj && !binvec && !gltf)) {
        dbg("json: name '%s' obj '%s' binvec '%s' tex '%s'\n",
            name, obj, binvec, tex);
        return -1;
    }
    if (!gltf && !tex)
        return -1;
    
    /* XXX: obj and binvec will bitrot pretty quickly */
    if (obj) {
        libh = lib_request_obj(obj, scene);
        txm = model3dtx_new(scene->_model, tex);
        ref_put(scene->_model);
        scene_add_model(scene, txm);
        ref_put_last(libh);
    } else if (binvec) {
        libh = lib_request_bin_vec(binvec, scene);
        txm = model3dtx_new(scene->_model, tex);
        ref_put(scene->_model);
        scene_add_model(scene, txm);
        ref_put_last(libh);
    } else if (gltf) {
        gd = gltf_load(scene, gltf);
        if (gltf_get_meshes(gd) > 1) {
            int i, root = gltf_root_mesh(gd);

            collision = gltf_mesh_by_name(gd, "collision");
            if (root < 0)
                for (i = 0; i < gltf_get_meshes(gd); i++)
                    if (i != collision) {
                        gltf_instantiate_one(gd, i);
                        break; /* XXX: why? */
                    }
            /* In the absence of a dedicated collision mesh, use the main one */
            if (collision < 0)
                collision = 0;
        } else {
            gltf_instantiate_one(gd, 0);
            collision = 0;
        }
        txm = mq_model_last(&scene->mq);
        txm->model->cull_face = cull_face;
        txm->model->alpha_blend = alpha_blend;
    }

    /* XXX: get rid of scene->_model, we don't have a reference to it */
    model3d_set_name(scene->_model, name);

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
            else if (p->tag == JSON_NUMBER && !strcmp(p->key, "length"))
                geom_length = p->number_;
            else if (p->tag == JSON_STRING && !strcmp(p->key, "geom")) {
                if (!strcmp(p->string_, "trimesh"))
                    class = dTriMeshClass;
                else if (!strcmp(p->string_, "sphere"))
                    class = dSphereClass;
                else if (!strcmp(p->string_, "capsule"))
                    class = dCapsuleClass;
            } else if (p->tag == JSON_STRING && !strcmp(p->key, "type")) {
                if (!strcmp(p->string_, "body"))
                    ptype = PHYS_BODY;
                else if (!strcmp(p->string_, "geom"))
                    ptype = PHYS_GEOM;
            }
        }
    }

    if (ent || ch) {
        JsonNode *it = ent ? ent : ch;
        for (; it; it = it->next) {
            struct character *c = NULL;
            struct entity3d  *e;
            JsonNode *pos;

            if (it->tag != JSON_ARRAY)
                continue; /* XXX: in fact, no */

            if (ch) {
                c = character_new(txm, scene);
                // if (!scene->control)
                //     scene->control = c;
                e = c->entity;
            } else {
                e = entity3d_new(txm);
            }
            pos = it->children.head;
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

            if (terrain_clamp) {
                e->dy = terrain_height(scene->terrain, e->dx, e->dz);
                // trace("clamped '%s' to %f,%f,%f\n", entity_name(e), e->dx, e->dy, e->dz);
            }

            if (c) {
                c->pos[0] = e->dx;
                c->pos[1] = e->dy;
                c->pos[2] = e->dz;
            }

            /* XXX: if it's not a gltf, we won't have TriMesh collision data any more */
            if (gd && class == dTriMeshClass) {
                gltf_mesh_data(gd, collision, &e->collision_vx, &e->collision_vxsz,
                               &e->collision_idx, &e->collision_idxsz, NULL, NULL, NULL, NULL);
            }

            mat4x4_translate_in_place(e->mx->m, e->dx, e->dy, e->dz);
            mat4x4_scale_aniso(e->mx->m, e->mx->m, e->scale, e->scale, e->scale);
            e->visible        = 1;
            model3dtx_add_entity(txm, e);

            /*
             * XXX: This kinda requires that "physics" goes before "entity"
             */
            if (phys) {
                entity3d_add_physics(e, mass, class, ptype, geom_off, geom_radius, geom_length);
                e->phys_body->bounce = bounce;
                e->phys_body->bounce_vel = bounce_vel;
            }
            trace("added '%s' entity at %f,%f,%f scale %f\n", name, e->dx, e->dy, e->dz, e->scale);
        }
    } else {
        create_entities(txm);
    }

    if (gd)
        gltf_free(gd);

    dbg("loaded model '%s'\n", name);

    return 0;
}

static void scene_onload(struct lib_handle *h, void *buf)
{
    struct scene *scene = buf;
    char         msg[256];
    LOCAL(JsonNode, node);
    JsonNode     *p, *m;

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
    ref_put(h);
    scene_control_next(scene);
}

int scene_load(struct scene *scene, const char *name)
{
    struct lib_handle *lh = lib_request(RES_ASSET, name, scene_onload, scene);

    err_on(lh->state != RES_LOADED);
    ref_put_last(lh);
    click = sound_load("stapler.ogg");

    return 0;
}

void scene_done(struct scene *scene)
{
    struct model3dtx *txmodel, *ittxm;
    struct entity3d  *ent, *itent;

    terrain_done(scene->terrain);
    ref_put_last(scene->camera->ch);

    mq_release(&scene->mq);
}
