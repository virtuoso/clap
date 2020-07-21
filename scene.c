#include "messagebus.h"
#include "shader.h"
#include "model.h"
#include "scene.h"
#include "json.h"

static void scene_camera_autopilot(struct scene *s)
{
    s->camera.pos[0] = 12 * cos(to_radians(s->frames_total)/4);
    s->camera.pos[1] = 2 * sin(to_radians(s->frames_total)/10) + 4;
    s->camera.pos[2] = 12 * sin(to_radians(s->frames_total)/4) + 9.0;
    s->camera.yaw    = -(float)((s->frames_total) % 1440)/4 + 90.0;
    s->camera.moved++;
}

static void scene_focus_next(struct scene *s)
{
    struct model3dtx *next_txm;

    if (!s->focus)
        goto first_txm;

    if (s->focus != list_last_entry(&s->focus->txmodel->entities, struct entity3d, entry)) {
        s->focus = list_next_entry(s->focus, entry);
        return;
    }

    if (s->focus->txmodel != list_last_entry(&s->txmodels, struct model3dtx, entry)) {
        next_txm = list_next_entry(s->focus->txmodel, entry);
        goto first_entry;
    }

first_txm:
    next_txm = list_first_entry(&s->txmodels, struct model3dtx, entry);
first_entry:
    s->focus = list_first_entry(&next_txm->entities, struct entity3d, entry);
}

static void scene_focus_prev(struct scene *s)
{
    struct model3dtx *next_txm;

    if (!s->focus)
        goto last_txm;

    if (s->focus != list_first_entry(&s->focus->txmodel->entities, struct entity3d, entry)) {
        s->focus = list_prev_entry(s->focus, entry);
        return;
    }

    if (s->focus->txmodel != list_last_entry(&s->txmodels, struct model3dtx, entry)) {
        next_txm = list_prev_entry(s->focus->txmodel, entry);
        goto last_entry;
    }

last_txm:
    next_txm = list_last_entry(&s->txmodels, struct model3dtx, entry);
last_entry:
    s->focus = list_last_entry(&next_txm->entities, struct entity3d, entry);
}

static void scene_focus_cancel(struct scene *s)
{
    s->focus = NULL;
}

void scene_camera_calc(struct scene *s)
{
    float scalev[3];
    int i;

    if (s->autopilot)
        scene_camera_autopilot(s);
    if (!s->camera.moved)
        return;

    for (i = 0; i < 3; i++)
        scalev[i] = s->camera.zoom ? 3.0 : 1.0;

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
        gl_title("One Hand Clap @%d FPS camera [%f,%f,%f] [%f/%f]", s->FPS,
                 s->camera.pos[0], s->camera.pos[1], s->camera.pos[2],
                 s->camera.pitch, s->camera.yaw);
}

static int scene_handle_input(struct message *m, void *data)
{
    struct scene *s = data;

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
    if (m->input.right) {
        if (s->focus)
            s->focus->dx += 0.1;
        else
            s->camera.pos[0] += 0.1;
    }
    if (m->input.left) {
        if (s->focus)
            s->focus->dx -= 0.1;
        else
            s->camera.pos[0] -= 0.1;
    }
    if (m->input.up) {
        if (s->focus)
            s->focus->dz += 0.1;
        else
            s->camera.pos[2] += 0.1;
    }
    if (m->input.down) {
        if (s->focus)
            s->focus->dz -= 0.1;
        else
            s->camera.pos[2] -= 0.1;
    }
    if (m->input.pitch_up && s->camera.pitch < 90)
        s->camera.pitch += 5;
    if (m->input.pitch_down && s->camera.pitch > -90)
        s->camera.pitch -= 5;
    if (m->input.yaw_right) {
        s->camera.yaw += 10;
        if (s->camera.yaw > 180)
            s->camera.yaw -= 360;
    }
    if (m->input.yaw_left) {
        s->camera.yaw -= 10;
        if (s->camera.yaw <= -180)
            s->camera.yaw += 360;
    }
    s->camera.zoom = !!(m->input.zoom);
    s->camera.pos[1] += m->input.delta_y / 100.0;
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
    scene->light.pos[0] = 30.0 * cos(to_radians((float)scene->frames_total / 4.0));
    scene->light.pos[1] = 30.0 * sin(to_radians((float)scene->frames_total / 4.0));
    scene->light.pos[2] = 0.0;
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
    list_init(&scene->txmodels);

    subscribe(MT_INPUT, scene_handle_input, scene);

    return 0;
}

struct scene_config {
    char            name[256];
    struct model    *model;
    unsigned long   nr_model;
};

static int model_new_from_json(struct scene *scene, JsonNode *node)
{
    char *name = NULL, *obj = NULL, *binvec = NULL, *tex = NULL;
    JsonNode *p, *ent = NULL;
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
        else if (p->tag == JSON_ARRAY && !strcmp(p->key, "entity"))
            ent = p->children.head;
    }

    if (!name || (!!obj == !!binvec) || !tex) {
        dbg("json: name '%s' obj '%s' binvec '%s' tex '%s'\n",
            name, obj, binvec, tex);
        return -1;
    }
    
    if (obj) {
        lib_request_obj(obj, scene);
    } else {
        lib_request_bin_vec(binvec, scene);
    }
    scene->_model->name = strdup(name);
    txm = model3dtx_new(scene->_model, tex);
    ref_put(&scene->_model->ref);
    scene_add_model(scene, txm);

    if (ent) {
        for (; ent; ent = ent->next) {
            struct entity3d *e;
            JsonNode *pos;

            e = entity3d_new(txm);
            if (ent->tag != JSON_ARRAY)
                continue; /* XXX: in fact, no */

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

            mat4x4_translate_in_place(e->base_mx->m, e->dx, e->dy, e->dz);
            mat4x4_scale_aniso(e->base_mx->m, e->base_mx->m, e->scale, e->scale, e->scale);
            e->mx             = e->base_mx;
            e->visible        = 1;
            model3dtx_add_entity(txm, e);
            trace("added '%s' entity at %f,%f,%f scale %f\n", name, e->dx, e->dy, e->dz, e->scale);
        }
    } else {
        create_entities(txm);
    }
    ref_put(&txm->ref);
    dbg("loaded model '%s'\n", name);

    return 0;
}

static void scene_onload(struct lib_handle *h, void *buf)
{
    struct scene *scene = buf;
    char          msg[256];
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
    lib_request(RES_ASSET, name, scene_onload, scene);

    return 0;
}
