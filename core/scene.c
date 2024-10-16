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
    camera_add_yaw(s->camera, -s->ang_speed / 5);
    s->camera->ch->moved++;
}

static void scene_control_next(struct scene *s)
{
    struct character *first, *last, *prev;

    if (list_empty(&s->characters))
        return;

    prev = s->control;
    first = list_first_entry(&s->characters, struct character, entry);
    last = list_last_entry(&s->characters, struct character, entry);
    if (!s->control || s->control == last)
        s->control = first;
    else
        s->control = list_next_entry(s->control, entry);

    if (s->control == prev)
        return;

    prev->camera = NULL;
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
    struct model3d *m = model3d_new_cube(list_first_entry(&s->shaders, struct shader_prog, entry));
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
    camera_setup(s->camera);
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

    struct camera *cam = &s->cameras[camera];

    /* circle the character s->control */
    if (s->control != s->cameras[camera].ch &&
        (camera_has_moved(cam) || s->control->moved)) {

        // float dist           = s->cameras[camera].zoom ? 1 : 10;
        float x              = s->control->pos[0];
        float y              = s->control->pos[1] + entity3d_aabb_Y(s->control->entity) / 4 * 3;
        float z              = s->control->pos[2];
        camera_position(cam, x, y, z, s->cameras[camera].ch->pos);
        //s->control->moved    = 0; /* XXX */
    }

    camera_reset_movement(cam);
    s->cameras[camera].ch->moved = 0;
    trace("camera: %f/%f/%f zoom: %d\n", s->cameras[camera].ch->pos[0], s->cameras[camera].ch->pos[1], s->cameras[camera].ch->pos[2], s->cameras[camera].zoom);

    //free(s->view_mx);
    //s->view_mx = transmx_new(negpos, 0.0, 0.0, 0.0, 1.0);
    mat4x4_identity(s->cameras[camera].view_mx->m);
    mat4x4_rotate_X(s->cameras[camera].view_mx->m, s->cameras[camera].view_mx->m, to_radians(s->cameras[camera].current_pitch));
    mat4x4_rotate_Y(s->cameras[camera].view_mx->m, s->cameras[camera].view_mx->m, to_radians(s->cameras[camera].current_yaw));
    mat4x4_scale_aniso(s->cameras[camera].view_mx->m, s->cameras[camera].view_mx->m, scalev[0], scalev[1], scalev[2]);
    //mat4x4_scale(s->cameras[camera].view_mx->m, scalev, 1.0);
    mat4x4_translate_in_place(s->cameras[camera].view_mx->m, -s->cameras[camera].ch->pos[0], -s->cameras[camera].ch->pos[1], -s->cameras[camera].ch->pos[2]);

    mat4x4_invert(s->cameras[camera].inv_view_mx->m, s->cameras[camera].view_mx->m);
    camera_calc_frustum(cam, s->proj_mx->m);
#ifndef CONFIG_FINAL
    if (!(s->frames_total & 0xf) && camera == 0)
        gl_title("One Hand Clap @%d FPS camera0 [%f,%f,%f] [%f/%f]", s->fps.fps_coarse,
                 s->cameras[camera].ch->pos[0], s->cameras[camera].ch->pos[1], s->cameras[camera].ch->pos[2],
                 s->cameras[camera].current_pitch, s->cameras[camera].current_yaw);
#endif
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
    int ret = 0;

    if (m->cmd.toggle_modality) {
        s->ui_is_on = !s->ui_is_on;
        ret = MSG_HANDLED;
    }

    if (s->ui_is_on)
        goto out;

    if (m->cmd.toggle_autopilot) {
        s->autopilot ^= 1;
        ret = MSG_HANDLED;
    }

out:
    return ret;
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
    if (m->input.debug_action || (m->input.pad_lb && m->input.pad_rb)) {
        debug_camera_action(s->camera);
    }
    if (m->input.exit)
        gl_request_exit();
#ifndef CONFIG_FINAL
    if (m->input.tab || m->input.stick_r)
        scene_control_next(s);
    if (m->input.autopilot)
        s->autopilot ^= 1;
    if (m->input.focus_next)
        scene_focus_next(s);
    if (m->input.focus_prev)
        scene_focus_prev(s);
    if (m->input.focus_cancel)
        scene_focus_cancel(s);
#endif
    if (m->input.resize)
        gl_resize(m->input.x, m->input.y);
    if (m->input.fullscreen) {
        if (s->fullscreen)
            gl_leave_fullscreen();
        else
            gl_enter_fullscreen();
        s->fullscreen ^= 1;
        trace("fullscreen: %d\n", s->fullscreen);
    }

#ifndef CONFIG_FINAL
    if (m->input.verboser) {
        struct message m;

        msg("toggle noise\n");
        /* XXX: factor out */
        memset(&m, 0, sizeof(m));
        m.type = MT_COMMAND;
        m.cmd.toggle_noise = 1;
        message_send(&m);
    }
#endif

    if (s->ui_is_on)
        return 0;

    character_handle_input(s->control, s, m);
    //trace("## control is grounded: %d\n", character_is_grounded(s->control, s));

    return 0;
}

int scene_add_model(struct scene *s, struct model3dtx *txm)
{
    mq_add_model(&s->mq, txm);
    return 0;
}

void light_set_pos(struct light *light, int idx, float pos[3])
{
    int i;

    for (i = 0; i < 3; i++)
        light->pos[idx * 3 + i] = pos[i];
}

void light_set_color(struct light *light, int idx, float color[3])
{
    int i;

    for (i = 0; i < 3; i++)
        light->color[idx * 3 + i] = color[i];
}

void light_set_attenuation(struct light *light, int idx, float attenuation[3])
{
    int i;

    for (i = 0; i < 3; i++)
        light->attenuation[idx * 3 + i] = attenuation[i];
}

int scene_get_light(struct scene *scene)
{
    if (scene->nr_lights == LIGHTS_MAX)
        return -1;

    return scene->nr_lights++;
}

void scene_update(struct scene *scene)
{
    struct model3dtx *txm;
    struct entity3d  *ent;

    mq_update(&scene->mq);
}

int scene_init(struct scene *scene)
{
    memset(scene, 0, sizeof(*scene));
    scene->proj_mx      = mx_new();
    scene->auto_yoffset = 4.0;
    scene->near_plane   = 0.1;
    scene->far_plane    = 1000.0;
    mq_init(&scene->mq, scene);
    list_init(&scene->characters);
    list_init(&scene->instor);
    list_init(&scene->shaders);
    list_init(&scene->debug_draws);

    int i;
    for (i = 0; i < LIGHTS_MAX; i++) {
        float attenuation[3] = { 1, 0, 0 };
        light_set_attenuation(&scene->light, i, attenuation);
    }

    subscribe(MT_INPUT, scene_handle_input, scene);
    subscribe(MT_COMMAND, scene_handle_command, scene);

    scene->initialized = true;

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
    double mass = 1.0, bounce = 0.0, bounce_vel = dInfinity, geom_off = 0.0, geom_radius = 1.0, geom_length = 1.0, speed = 0.75;
    char *name = NULL, *gltf = NULL, *tex = NULL;
    bool terrain_clamp = false, cull_face = true, alpha_blend = false, jump = false, can_sprint = false;
    JsonNode *p, *ent = NULL, *ch = NULL, *phys = NULL, *anis = NULL, *light = NULL;
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
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "can_sprint"))
            can_sprint = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "jump"))
            jump = p->bool_;
        else if (p->tag == JSON_ARRAY && !strcmp(p->key, "entity"))
            ent = p->children.head;
        else if (p->tag == JSON_ARRAY && !strcmp(p->key, "character"))
            ch = p->children.head;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "animations"))
            anis = p;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "speed"))
            speed = p->number_;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "light"))
            light = p;
    }

    if (!name || !gltf) {
        dbg("json: name '%s' or gltf '%s' missing\n", name, gltf);
        return -1;
    }
    
    gd = gltf_load(scene, gltf);
    if (!gd) {
        warn("Error loading GLTF '%s'\n", gltf);
        return -1;
    }

    if (gltf_get_meshes(gd) > 1) {
        int i, root = gltf_root_mesh(gd);

        collision = gltf_mesh_by_name(gd, "collision");
        if (root < 0)
            for (i = 0; i < gltf_get_meshes(gd); i++)
                if (i != collision) {
                    if (gltf_instantiate_one(gd, i)) {
                        gltf_free(gd);
                        return -1;
                    }
                    break; /* XXX: why? */
                }
        /* In the absence of a dedicated collision mesh, use the main one */
        if (collision < 0)
            collision = 0;
    } else {
        if (gltf_instantiate_one(gd, 0)) {
            gltf_free(gd);
            return -1;
        }
        collision = 0;
    }
    txm = mq_model_last(&scene->mq);
    txm->model->cull_face = cull_face;
    txm->model->alpha_blend = alpha_blend;

    model3d_set_name(txm->model, name);

    if (phys) {
        for (p = phys->children.head; p; p = p->next) {
            if (p->tag == JSON_NUMBER && !strcmp(p->key, "bounce"))
                bounce = p->number_;
            else if (p->tag == JSON_NUMBER && !strcmp(p->key, "bounce_vel"))
                bounce_vel = p->number_;
            else if (p->tag == JSON_NUMBER && !strcmp(p->key, "mass"))
                mass = p->number_;
            else if (p->tag == JSON_NUMBER && !strcmp(p->key, "yoffset"))
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

        /* XXX: if it's not a gltf, we won't have TriMesh collision data any more */
        if (gd && class == dTriMeshClass) {
            gltf_mesh_data(gd, collision, &txm->model->collision_vx, &txm->model->collision_vxsz,
                           &txm->model->collision_idx, &txm->model->collision_idxsz, NULL, NULL, NULL, NULL);
        }
    }

    if (ent || ch) {
        JsonNode *it = ent ? ent : ch;
        for (; it; it = it->next) {
            struct character *c = NULL;
            struct entity3d  *e;
            JsonNode *pos, *jpos;

            if (it->tag != JSON_OBJECT)
                continue; /* XXX: in fact, no */

            if (ch) {
                c = character_new(txm, scene);
                // if (!scene->control)
                //     scene->control = c;
                e = c->entity;
                e->skip_culling = true;
                c->can_sprint = can_sprint;
                c->jumping = jump;
            } else {
                e = entity3d_new(txm);
            }
            jpos = json_find_member(it, "position");
            if (jpos->tag != JSON_ARRAY)
                continue;

            pos = jpos->children.head;
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
            /* rotation: optional */
            pos = pos->next;
            if (pos && pos->tag == JSON_NUMBER)
                e->ry = to_radians(pos->number_);
            else
                e->ry = 0;

            double _light_color[3];
            jpos = json_find_member(it, "light_color");
            if (jpos && jpos->tag == JSON_ARRAY) {
                e->light_idx = scene_get_light(scene);
                if (e->light_idx < 0)
                    goto light_done;

                if (json_double_array(jpos, _light_color, 3))
                    goto light_done;

                float light_color[3] = { _light_color[0], _light_color[1], _light_color[2] };
                light_set_color(&scene->light, e->light_idx, light_color);
            }

            double _light_offset[3];
            jpos = json_find_member(it, "light_offset");
            if (jpos && jpos->tag == JSON_ARRAY && e->light_idx >= 0) {
                if (json_double_array(jpos, _light_offset, 3))
                    goto light_done;

                e->light_off[0] = _light_offset[0];
                e->light_off[1] = _light_offset[1];
                e->light_off[2] = _light_offset[2];
            }

            double _light_attenuation[3];
            jpos = json_find_member(it, "light_attenuation");
            if (jpos && jpos->tag == JSON_ARRAY && e->light_idx >= 0) {
                if (!json_double_array(jpos, _light_attenuation, 3)) {
                    float light_attenuation[3] = { _light_attenuation[0], _light_attenuation[1], _light_attenuation[2] };
                    light_set_attenuation(&scene->light, e->light_idx, light_attenuation);
                }
            }

light_done:
            if (terrain_clamp)
                phys_ground_entity(e);

            if (c) {
                c->pos[0] = e->dx;
                c->pos[1] = e->dy;
                c->pos[2] = e->dz;
                c->speed  = speed;
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

            if (entity_animated(e) && anis) {
                for (p = anis->children.head; p; p = p->next) {
                    if (p->tag != JSON_STRING)
                        continue;

                    struct model3d *m = e->txmodel->model;
                    int idx = animation_by_name(m, p->string_);

                    if (idx < 0)
                        continue;
                    dbg("action '%s': animation '%s'\n", p->key, p->string_);
                    free(m->anis.x[idx].name);
                    m->anis.x[idx].name = strdup(p->key);
                }

                if (c &&
                    animation_by_name(e->txmodel->model, "start") >= 0 &&
                    animation_by_name(e->txmodel->model, "start_to_idle") >= 0) {
                        c->anictl.state = UINT_MAX;
                        c->state = CS_START;
                        animation_push_by_name(e, scene, "start", true, true);
                }
            }
        }
    } else {
        struct instantiator *instor, *iter;
        struct entity3d *e;

        list_for_each_entry_iter(instor, iter, &scene->instor, entry) {
            if (!strcmp(txmodel_name(txm), instor->name)) {
                e = instantiate_entity(txm, instor, true, 0.5, scene);
                list_del(&instor->entry);
                free(instor);

                if (phys) {
                    entity3d_add_physics(e, mass, class, ptype, geom_off, geom_radius, geom_length);
                    e->phys_body->bounce = bounce;
                    e->phys_body->bounce_vel = bounce_vel;
                }
            }
        }
    }

    if (gd)
        gltf_free(gd);

    dbg("loaded model '%s'\n", name);

    return 0;
}

static int scene_add_light_from_json(struct scene *s, JsonNode *light)
{
    if (light->tag != JSON_OBJECT)
        return -1;

    JsonNode *jpos = json_find_member(light, "position");
    JsonNode *jcolor = json_find_member(light, "color");
    if (!jpos || jpos->tag != JSON_ARRAY || !jcolor || jcolor->tag != JSON_ARRAY)
        return -1;

    double pos[3], color[3];
    if (json_double_array(jpos, pos, 3) ||
        json_double_array(jcolor, color, 3))
        return -1;

    int idx = scene_get_light(s);
    if (idx < 0)
        return -1;

    float fpos[3] = { pos[0], pos[1], pos[2] };
    float fcolor[3] = { color[0], color[1], color[2] };
    light_set_pos(&s->light, idx, fpos);
    light_set_color(&s->light, idx, fcolor);

    return 0;
}

static void scene_onload(struct lib_handle *h, void *buf)
{
    struct scene *scene = buf;
    char         msg[256];
    LOCAL(JsonNode, node);
    JsonNode     *p, *m;

    if (h->state == RES_ERROR) {
        err("couldn't load scene %s\n", h->name);
        goto out;
    }

    node = json_decode(h->buf);
    if (!node) {
        err("couldn't parse '%s'\n", h->name);
        h->state = RES_ERROR;
        goto out;
    }

    if (!json_check(node, msg)) {
        err("error parsing '%s': '%s'\n", h->name, msg);
        h->state = RES_ERROR;
        goto out;
    }

    if (node->tag != JSON_OBJECT) {
        err("parse error in '%s'\n", h->name);
        h->state = RES_ERROR;
        goto out;
    }

    for (p = node->children.head; p; p = p->next) {
        if (!strcmp(p->key, "name")) {
            if (p->tag != JSON_STRING) {
                err("parse error in '%s'\n", h->name);
                h->state = RES_ERROR;
                goto out;
            }
            scene->name = strdup(p->string_);
        } else if (!strcmp(p->key, "model")) {
            if (p->tag != JSON_ARRAY) {
                err("parse error in '%s'\n", h->name);
                h->state = RES_ERROR;
                goto out;
            }

            for (m = p->children.head; m; m = m->next) {
                model_new_from_json(scene, m);
            }
        } else if (!strcmp(p->key, "light") && p->tag == JSON_ARRAY) {
            JsonNode *light;

            for (light = p->children.head; light; light = light->next)
                scene_add_light_from_json(scene, light);
        }
    }
    dbg("loaded scene: '%s'\n", scene->name);
out:
    ref_put(h);

    if (h->state == RES_LOADED)
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
    struct instantiator *instor;
    struct character *iter, *ch;

    list_for_each_entry_iter(ch, iter, &scene->characters, entry)
        ref_put_last(ch);

    while (!list_empty(&scene->instor)) {
        instor = list_first_entry(&scene->instor, struct instantiator, entry);
        list_del(&instor->entry);
        free(instor);
    }

    if (scene->terrain)
        terrain_done(scene->terrain);

    mq_release(&scene->mq);

    struct shader_prog *prog, *it;

    /*
     * clean up the shaders that weren't freed by model3d_drop()
     * via mq_release()
     */
    list_for_each_entry_iter(prog, it, &scene->shaders, entry)
        ref_put(prog);

    free(scene->name);
}
