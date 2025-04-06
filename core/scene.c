// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include "messagebus.h"
#include "character.h"
#include "clap.h"
#include "gltf.h"
#include "physics.h"
#include "primitives.h"
#include "shader.h"
#include "loading-screen.h"
#include "model.h"
#include "scene.h"
#include "sound.h"
#include "json.h"
#include "ui-debug.h"
#include "ui.h"

static void scene_control_next(struct scene *s)
{
    struct character *first, *last, *prev, *current;

    current = scene_control_character(s);
    if (list_empty(&s->characters) || !current)
        return;

    prev = current;
    first = list_first_entry(&s->characters, struct character, entry);
    last = list_last_entry(&s->characters, struct character, entry);
    if (current == last)
        s->control = first->entity;
    else
        s->control = list_next_entry(current, entry)->entity;

    current = s->control->priv;
    if (current == prev)
        return;

    prev->camera = NULL;
    current->camera = s->camera;
    current->moved++;
    s->camera->dist = 10;
    s->camera->ch->moved++;

    trace("scene control at: '%s'\n", entity_name(s->control));
    dbg("scene control at: '%s'\n", entity_name(s->control));
}

#ifndef CONFIG_FINAL
static void scene_focus_next(struct scene *s)
{
    model3dtx *next_txm;

    if (!s->focus) {
        next_txm = mq_nonempty_txm_next(&s->mq, NULL, true);
        /* nothing to focus on */
        if (!next_txm)
            return;

        goto first_entry;
    }

    if (s->focus != list_last_entry(&s->focus->txmodel->entities, entity3d, entry)) {
        s->focus = list_next_entry(s->focus, entry);
        return;
    }

    next_txm = mq_nonempty_txm_next(&s->mq, s->focus->txmodel, true);
    if (!next_txm)
        return;

first_entry:
    s->focus = list_first_entry(&next_txm->entities, entity3d, entry);
}

static void scene_focus_prev(struct scene *s)
{
    model3dtx *next_txm;

    if (!s->focus) {
        next_txm = mq_nonempty_txm_next(&s->mq, NULL, false);
        if (!next_txm)
            return;
        goto last_entry;
    }

    if (s->focus != list_first_entry(&s->focus->txmodel->entities, entity3d, entry)) {
        s->focus = list_prev_entry(s->focus, entry);
        return;
    }

    next_txm = mq_nonempty_txm_next(&s->mq, s->focus->txmodel, false);
    if (!next_txm)
        return;
last_entry:
    s->focus = list_last_entry(&next_txm->entities, entity3d, entry);
}

static void scene_focus_cancel(struct scene *s)
{
    s->focus = NULL;
}
#endif /* CONFIG_FINAL */

bool scene_camera_follows(struct scene *s, struct character *ch)
{
    return scene_control_character(s) == ch && ch != s->camera->ch;
}

cres(int) scene_camera_add(struct scene *s)
{
    cresp(shader_prog) prog_res = pipeline_shader_find_get(s->pl, "model");
    if (IS_CERR(prog_res))
        return cres_error_cerr(int, prog_res);

    model3d *m = model3d_new_cube(ref_pass(prog_res.val));
    if (!m)
        return cres_error(int, CERR_NOMEM);

    model3d_set_name(m, "camera");

    cresp(model3dtx) txmres = ref_new_checked(model3dtx, .model = ref_pass(m), .tex = transparent_pixel());
    if (IS_CERR(txmres))
        return cres_error_cerr(int, txmres);

    cresp(character) chres = ref_new_checked(character, .txmodel = txmres.val, .scene = s);
    if (IS_CERR(chres)) {
        ref_put(txmres.val);
        return cres_error_cerr(int, chres);
    }

    s->camera = &s->cameras[s->nr_cameras];
    s->camera->view.main.near_plane  = 0.1;
    s->camera->view.main.far_plane   = 500.0;
    s->camera->view.fov              = to_radians(70);
    s->camera->ch = chres.val;
    entity3d *entity = character_entity(s->camera->ch);
    entity3d_visible(entity, 0);
    s->control = entity;
    scene_add_model(s, entity->txmodel);
    ref_put(entity->txmodel);

    camera_setup(s->camera);
    s->camera->ch->moved++;
    s->camera->dist = 10;

    return cres_val(int, s->nr_cameras++);
}

#ifndef CONFIG_FINAL
static int input_text_callback(ImGuiInputTextCallbackData *data)
{
    return 0;
}

static void scene_parameters_debug(struct scene *scene, int cam_idx)
{
    debug_module *dbgm = ui_igBegin(DEBUG_SCENE_PARAMETERS, ImGuiWindowFlags_AlwaysAutoResize);
    struct camera *cam = &scene->camera[cam_idx];

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        igSliderFloat("near plane", &cam->view.main.near_plane, 0.1, 10.0, "%f", ImGuiSliderFlags_ClampOnInput);
        igSliderFloat("far plane", &cam->view.main.far_plane, 10.0, 300.0, "%f", ImGuiSliderFlags_ClampOnInput);

        float fov = to_degrees(cam->view.fov);
        igSliderFloat("FOV", &fov, 30.0, 120.0, "%f", ImGuiSliderFlags_ClampOnInput);
        cam->view.fov = to_radians(fov);

        igCheckbox("shadow outline", &scene->render_options.shadow_outline);
        if (scene->render_options.shadow_outline)
            igSliderFloat("shadow outline threshold", &scene->render_options.shadow_outline_threshold,
                          0.0, 1.0, "%.02f", ImGuiSliderFlags_ClampOnInput);
        igCheckbox("shadow msaa", &scene->render_options.shadow_msaa);
        igCheckbox("model msaa", &scene->render_options.model_msaa);
        igCheckbox("edge sobel", &scene->render_options.edge_sobel);
        igCheckbox("edge antialiasing", &scene->render_options.edge_antialiasing);
        if (!scene->render_options.edge_sobel) {
            igText("Laplace kernel size");
            igSameLine(0.0, 0.0);
            igRadioButton_IntPtr("3x3", &scene->render_options.laplace_kernel, 3);
            igSameLine(0.0, 0.0);
            igRadioButton_IntPtr("5x5", &scene->render_options.laplace_kernel, 5);
        }
        igCheckbox("debug draws", &scene->render_options.debug_draws_enabled);
        igCheckbox("collision draws", &scene->render_options.collision_draws_enabled);
        igCheckbox("aabb draws", &scene->render_options.aabb_draws_enabled);
        igInputText("scene name", scene->name, sizeof(scene->name), ImGuiInputFlags_Tooltip,
                    input_text_callback, NULL);
        if (igButton("save level", (ImVec2){}))
            scene_save(scene, NULL);
        scene->proj_update++;
    }

    ui_igEnd(DEBUG_SCENE_PARAMETERS);
}

static void light_debug(struct light *light, int idx)
{
    debug_module *dbgm = ui_igBegin(DEBUG_LIGHT, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    struct view *view = &light->view[idx];
    float *dir = &light->dir[3 * idx];

    if (dbgm->unfolded) {
        igSetNextItemWidth(400);
        igSliderFloat3("light", dir, -500, 500, "%f", 0);
        igColorPicker3("color", &light->color[3 * idx], ImGuiColorEditFlags_DisplayRGB);
        if (ui_igVecTableHeader("view positions", 3)) {
            ui_igVecRow(dir, 3, "direction");
            igEndTable();
        }
        ui_igMat4x4(view->main.view_mx, "view matrix");
    }

    ui_igEnd(DEBUG_LIGHT);
}

static void scene_characters_debug(struct scene *scene)
{
    debug_module *dbgm = ui_igBegin(DEBUG_CHARACTERS, ImGuiWindowFlags_AlwaysAutoResize);
    struct character *c;

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        list_for_each_entry(c, &scene->characters, entry) {
            const char *name = entity_name(c->entity);

            igText("character '%s'", name);
            igPushID_Str(name);
            igSliderFloat("jump forward", &c->jump_forward, 0.1, 10.0, "%f", ImGuiSliderFlags_AlwaysClamp);
            igSliderFloat("jump upward", &c->jump_upward, 0.1, 10.0, "%f", ImGuiSliderFlags_AlwaysClamp);
            igSliderFloat("speed", &c->speed, 0.1, 10.0, "%f", ImGuiSliderFlags_AlwaysClamp);
            igCheckbox("can jump", &c->can_jump);
            igCheckbox("can dash", &c->can_dash);
            igPopID();
            igSeparator();
        }
    }

    ui_igEnd(DEBUG_CHARACTERS);
}

static int scene_debug_draw(struct message *m, void *data)
{
    struct message_debug_draw *dd = &m->debug_draw;
    struct scene *s = data;

    if (!s->camera)
        return MSG_HANDLED;

    ImDrawList *draw = igGetBackgroundDrawList_Nil();

    struct subview *sv = &s->camera->view.main;
    mat4x4 mvp;
    mat4x4_mul(mvp, sv->proj_mx, sv->view_mx);

    ImU32 color = IM_COL32(
        (int)(dd->color[0] * 255),
        (int)(dd->color[1] * 255),
        (int)(dd->color[2] * 255),
        (int)(dd->color[3] * 255)
    );

    switch (dd->shape) {
        case DEBUG_DRAW_CIRCLE: if (s->render_options.collision_draws_enabled) {
            vec4 v0;
            vec3_dup(v0, dd->v0);
            v0[3] = 1.0;

            mat4x4_mul_vec4_post(v0, mvp, v0);
            vec3_scale(v0, v0, 1.0 / v0[3]);

            ImVec2 p0 = {
                .x = ((v0[0] + 1.0) / 2.0) * s->width,
                .y = ((1.0 - v0[1]) / 2.0) * s->height,
            };

            ImDrawList_AddCircleFilled(draw, p0, dd->radius, color, 16);

            break;
        }
        case DEBUG_DRAW_AABB: if (s->render_options.aabb_draws_enabled) {
            vec3 min, max;
            vec3_dup(min, dd->v0);
            vec3_dup(max, dd->v1);

            vec3 corners[8] = {
                { min[0], min[1], min[2] },
                { max[0], min[1], min[2] },
                { max[0], max[1], min[2] },
                { min[0], max[1], min[2] },
                { min[0], min[1], max[2] },
                { max[0], min[1], max[2] },
                { max[0], max[1], max[2] },
                { min[0], max[1], max[2] },
            };

            int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0}, // bottom face
                {4,5},{5,6},{6,7},{7,4}, // top face
                {0,4},{1,5},{2,6},{3,7}  // verticals
            };

            for (int i = 0; i < 12; i++) {
                vec4 a = { corners[edges[i][0]][0], corners[edges[i][0]][1], corners[edges[i][0]][2], 1.0 };
                vec4 b = { corners[edges[i][1]][0], corners[edges[i][1]][1], corners[edges[i][1]][2], 1.0 };

                mat4x4 mvp;
                mat4x4_mul(mvp, sv->proj_mx, sv->view_mx);
                mat4x4_mul_vec4_post(a, mvp, a);
                mat4x4_mul_vec4_post(b, mvp, b);

                if (a[3] < 1e-3 || b[3] < 1e-3)
                    continue;

                vec3_scale(a, a, 1.0 / a[3]);
                vec3_scale(b, b, 1.0 / b[3]);

                ImVec2 p0 = {
                    .x = ((a[0] + 1.0f) / 2.0f) * s->width,
                    .y = ((1.0f - a[1]) / 2.0f) * s->height
                };

                ImVec2 p1 = {
                    .x = ((b[0] + 1.0f) / 2.0f) * s->width,
                    .y = ((1.0f - b[1]) / 2.0f) * s->height
                };

                ImDrawList_AddLine(draw, p0, p1, color, 1.0f);
            }
            break;
        }

        default:
            break;
    }

    return MSG_HANDLED;
}
#else
static void scene_parameters_debug(struct scene *scene, int cam_idx) {}
static inline void light_debug(struct light *light, int idx) {}
static inline void scene_characters_debug(struct scene *scene) {}
#endif /* CONFIG_FINAL */

static void scene_camera_calc(struct scene *s, int camera)
{
    struct character *current = scene_control_character(s);
    struct camera *cam = &s->cameras[camera];

    if (!cam->ch->moved && current == cam->ch)
        return;

    vec3 target;
    vec3_dup(target, current->entity->pos);
    target[1] += entity3d_aabb_Y(current->entity) / 4 * 3;

    /* circle the entity s->control, unless it's camera */
    if (s->control != cam->ch->entity &&
        (camera_has_moved(cam) || current->moved)) {

        camera_position(cam, target[0], target[1], target[2]);
    }

    camera_reset_movement(cam);
    cam->ch->moved = 0;

    float *cam_pos = cam->ch->entity->pos;
    view_update_from_angles(&cam->view, cam_pos, cam->current_pitch, cam->current_yaw, cam->current_roll);
    view_calc_frustum(&cam->view);

    light_debug(&s->light, 0);

    /* only the first light source get to cast shadows for now */
    view_update_from_frustum(&s->light.view[0], &s->light.dir[0 * 3], &cam->view);
    view_calc_frustum(&s->light.view[0]);
#ifndef CONFIG_FINAL
    if (!(s->frames_total & 0xf) && camera == 0)
        display_title("One Hand Clap @%d FPS camera0 [%f,%f,%f] [%f/%f]",
                      clap_get_fps_coarse(s->clap_ctx),
                      cam_pos[0], cam_pos[1], cam_pos[2],
                      cam->current_pitch, cam->current_yaw);
#endif
}

void scene_cameras_calc(struct scene *s)
{
    int i;

    for (i = 0; i < s->nr_cameras; i++)
        scene_camera_calc(s, i);

    struct character *c;
    list_for_each_entry(c, &s->characters, entry)
        c->moved = 0;
}

void scene_characters_move(struct scene *s)
{
    struct character *ch;

    list_for_each_entry(ch, &s->characters, entry) {
        if (scene_control_character(s) == ch)
            motion_compute(&s->mctl);

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

out:
    return ret;
}

static int scene_handle_input(struct message *m, void *data)
{
    struct scene *s = data;

    if (m->input.debug_action || (m->input.pad_lb && m->input.pad_rb)) {
        debug_camera_action(s->camera);
    }
    if (m->input.exit)
        display_request_exit();
#ifndef CONFIG_FINAL
    if (m->input.tab || m->input.stick_r)
        scene_control_next(s);
    if (m->input.focus_next)
        scene_focus_next(s);
    if (m->input.focus_prev)
        scene_focus_prev(s);
    if (m->input.focus_cancel)
        scene_focus_cancel(s);
#endif
    if (m->input.resize)
        display_resize(m->input.x, m->input.y);
    if (m->input.fullscreen) {
        if (s->fullscreen)
            display_leave_fullscreen();
        else
            display_enter_fullscreen();
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

    motion_parse_input(&s->mctl, m);

    struct character *current = scene_control_character(s);
    if (current)
        character_handle_input(current, s, m);

    return 0;
}

int scene_add_model(struct scene *s, model3dtx *txm)
{
    mq_add_model(&s->mq, txm);
    return 0;
}

int scene_get_light(struct scene *scene)
{
    if (scene->nr_lights == LIGHTS_MAX)
        return -1;

    return scene->nr_lights++;
}

void scene_update(struct scene *scene)
{
    struct camera *cam = &scene->camera[0];

    scene_parameters_debug(scene, 0);
    scene_characters_debug(scene);

    if (scene->proj_update) {
        view_update_perspective_projection(&cam->view, scene->width, scene->height);
        scene->proj_update = 0;
    }

    mq_update(&scene->mq);

    motion_reset(&scene->mctl, scene);
}

cerr scene_init(struct scene *scene)
{
    memset(scene, 0, sizeof(*scene));
    scene->auto_yoffset = 4.0;
    mq_init(&scene->mq, scene);
    mq_init(&scene->debug_mq, scene);
    list_init(&scene->characters);
    list_init(&scene->instor);
    list_init(&scene->shaders);
    list_init(&scene->debug_draws);
    sfx_container_init(&scene->sfxc);

    int i;
    for (i = 0; i < LIGHTS_MAX; i++) {
        float attenuation[3] = { 1, 0, 0 };
        light_set_attenuation(&scene->light, i, attenuation);
    }

    scene->render_options.shadow_msaa = false;
    scene->render_options.laplace_kernel = 3;
    scene->render_options.edge_antialiasing = true;
    scene->render_options.shadow_outline = true;
    scene->render_options.shadow_outline_threshold = 0.4;

    cerr err;
    err = subscribe(MT_INPUT, scene_handle_input, scene);
    if (IS_CERR(err))
        return err;

    err = subscribe(MT_COMMAND, scene_handle_command, scene);
    if (IS_CERR(err))
        return err;

#ifndef CONFIG_FINAL
    err = subscribe(MT_DEBUG_DRAW, scene_debug_draw, scene);
    if (IS_CERR(err))
        err_cerr(err, "couldn't subscribe to debug draw messages\n");
#endif /* CONFIG_FINAL */

    scene->initialized = true;

    return CERR_OK;
}

static sfx *scene_get_sfx(struct scene *s, entity3d *e, const char *name)
{
    struct character *c = e->priv;
    sfx *sfx = NULL;

    if (c && !c->airborne) {
        if (c->collision)
            sfx = sfx_get(&c->collision->txmodel->model->sfxc, name);
        if (!sfx)
            sfx = sfx_get(&s->sfxc, name);
    }

    return sfx;
}

static void motion_frame_sfx(struct queued_animation *qa, entity3d *e, struct scene *s, double time)
{
    model3d *m = e->txmodel->model;
    double nr_segments = (double)m->anis.x[qa->animation].nr_segments;

    if (time < (double)(qa->sfx_state * 2 + 1) / nr_segments)
        return;

    qa->sfx_state++;

    const char *footstep = qa->sfx_state & 1 ? "footstep_right" : "footstep_left";
    sfx *sfx = scene_get_sfx(s, e, footstep);
    if (!sfx)
        return;

    sfx_play(sfx);
}

static void jump_to_motion_frame_sfx(struct queued_animation *qa, entity3d *e, struct scene *s, double time)
{
    if (time < 0.5 || qa->sfx_state)
        return;

    qa->sfx_state++;

    sfx *sfx = scene_get_sfx(s, e, "footstep_right");
    if (!sfx)
        return;

    sfx_play(sfx);
}

static void motion_stop_frame_sfx(struct queued_animation *qa, entity3d *e, struct scene *s, double time)
{
    if (qa->sfx_state)
        return;

    qa->sfx_state++;

    sfx *sfx = scene_get_sfx(s, e, "footstep_left");
    if (sfx)
        sfx_play(sfx);
}

static void fall_frame_sfx(struct queued_animation *qa, entity3d *e, struct scene *s, double time)
{
    if (qa->sfx_state)
        return;

    qa->sfx_state++;

    sfx *sfx = scene_get_sfx(s, e, "footstep_left");
    if (sfx)
        sfx_play(sfx);
}

const static struct animation_sfx {
    const char  *name;
    frame_fn    frame_sfx;
} animation_sfx[] = {
    { .name = "motion", .frame_sfx = motion_frame_sfx },
    { .name = "motion_stop", .frame_sfx = motion_stop_frame_sfx },
    { .name = "fall_to_idle", .frame_sfx = fall_frame_sfx },
    { .name = "jump_to_idle", .frame_sfx = fall_frame_sfx },
    { .name = "jump_to_motion", .frame_sfx = jump_to_motion_frame_sfx },
};

static cerr sfx_add_from_json(sfx_container *sfxc, sound_context *ctx, JsonNode *_sfx)
{
    if (_sfx->tag != JSON_STRING)
        return CERR_PARSE_FAILED;

    cresp(sfx) res = sfx_new(sfxc, _sfx->key, _sfx->string_, ctx);
    if (IS_CERR(res))
        return cerr_error_cres(res);

    return CERR_OK;
}

unsigned int total_models, nr_models;

static cerr model_new_from_json(struct scene *scene, JsonNode *node)
{
    double mass = 1.0, bounce = 0.0, bounce_vel = DINFINITY, geom_off = 0.0, geom_radius = 1.0, geom_length = 1.0, speed = 0.75;
    char *name = NULL, *gltf = NULL;
    bool terrain_clamp = false, cull_face = true, alpha_blend = false, can_jump = false, can_dash = false;
    bool outline_exclude = false, fix_origin = false;
    JsonNode *p, *ent = NULL, *ch = NULL, *phys = NULL, *anis = NULL, *sfx = NULL;
    geom_class class = GEOM_SPHERE;
    int collision = -1, motion_segments = 8;
    phys_type ptype = PHYS_BODY;
    struct gltf_data *gd = NULL;
    model3dtx  *txm;

    if (node->tag != JSON_OBJECT) {
        dbg("json: model is not an object\n");
        return CERR_PARSE_FAILED;
    }

    for (p = node->children.head; p; p = p->next) {
        if (p->tag == JSON_STRING && !strcmp(p->key, "name"))
            name = p->string_;
        else if (p->tag == JSON_STRING && !strcmp(p->key, "gltf"))
            gltf = p->string_;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "physics"))
            phys = p;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "terrain_clamp"))
            terrain_clamp = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "cull_face"))
            cull_face = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "alpha_blend"))
            alpha_blend = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "can_dash"))
            can_dash = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "can_jump"))
            can_jump = p->bool_;
        else if (p->tag == JSON_ARRAY && !strcmp(p->key, "entity"))
            ent = p->children.head;
        else if (p->tag == JSON_ARRAY && !strcmp(p->key, "character"))
            ch = p->children.head;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "animations"))
            anis = p;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "sfx"))
            sfx = p;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "speed"))
            speed = p->number_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "outline_exclude"))
            outline_exclude = p->bool_;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "motion_segments"))
            motion_segments = p->number_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "fix_origin"))
            fix_origin = p->bool_;
    }

    if (!name || !gltf) {
        dbg("json: name '%s' or gltf '%s' missing\n", name, gltf);
        return CERR_PARSE_FAILED;
    }
    
    gd = gltf_load(
        .mq         = &scene->mq,
        .pipeline   = scene->pl,
        .name       = gltf,
        .fix_origin = fix_origin
    );
    if (!gd) {
        warn("Error loading GLTF '%s'\n", gltf);
        return CERR_PARSE_FAILED;
    }

    if (gltf_get_meshes(gd) > 1) {
        int i, root = gltf_root_mesh(gd);

        collision = gltf_mesh_by_name(gd, "collision");
        if (root < 0) {
            for (i = 0; i < gltf_get_meshes(gd); i++)
                if (i != collision) {
                    cerr err = gltf_instantiate_one(gd, i);
                    if (IS_CERR(err)) {
                        gltf_free(gd);
                        return err;
                    }
                    break; /* XXX: why? */
                }
        } else {
            cerr err = gltf_instantiate_one(gd, root);
            if (IS_CERR(err)) {
                gltf_free(gd);
                return err;
            }
        }

        /* In the absence of a dedicated collision mesh, use the main one */
        if (collision < 0)
            collision = root ? root : 0;
    } else {
        cerr err = gltf_instantiate_one(gd, 0);
        if (IS_CERR(err)) {
            gltf_free(gd);
            return err;
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
                    class = GEOM_TRIMESH;
                else if (!strcmp(p->string_, "sphere"))
                    class = GEOM_SPHERE;
                else if (!strcmp(p->string_, "capsule"))
                    class = GEOM_CAPSULE;
            } else if (p->tag == JSON_STRING && !strcmp(p->key, "type")) {
                if (!strcmp(p->string_, "body"))
                    ptype = PHYS_BODY;
                else if (!strcmp(p->string_, "geom"))
                    ptype = PHYS_GEOM;
            }
        }
    }

    if (sfx)
        for (p = sfx->children.head; p; p = p->next)
            sfx_add_from_json(&txm->model->sfxc, clap_get_sound(scene->clap_ctx), p);

    if (ent || ch) {
        JsonNode *it = ent ? ent : ch;
        for (; it; it = it->next) {
            struct character *c = NULL;
            JsonNode *pos, *jpos;
            entity3d *e;

            if (it->tag != JSON_OBJECT)
                continue; /* XXX: in fact, no */

            if (ch) {
                c = ref_new(character, .txmodel = txm, .scene = scene);
                e = c->entity;
                e->skip_culling = true;
                c->can_dash = can_dash;
                c->can_jump = can_jump;
            } else {
                e = ref_new(entity3d, .txmodel = txm);
            }

            jpos = json_find_member(it, "outline_exclude");
            if (jpos && jpos->tag == JSON_BOOL)
                e->outline_exclude = jpos->bool_;
            else
                e->outline_exclude = outline_exclude;

            jpos = json_find_member(it, "position");
            if (jpos->tag != JSON_ARRAY)
                continue;

            vec3 e_pos = {};
            float ry = 0;

            pos = jpos->children.head;
            if (pos->tag != JSON_NUMBER)
                continue; /* XXX */
            e_pos[0] = pos->number_;
            pos = pos->next;      
            if (!pos || pos->tag != JSON_NUMBER)
                continue; /* XXX */
            e_pos[1] = pos->number_;
            pos = pos->next;
            if (!pos || pos->tag != JSON_NUMBER)
                continue; /* XXX */
            e_pos[2] = pos->number_;
            entity3d_position(e, e_pos);
            pos = pos->next;
            if (!pos || pos->tag != JSON_NUMBER)
                continue; /* XXX */
            entity3d_scale(e, pos->number_);

            /* rotation: optional */
            pos = pos->next;
            if (pos && pos->tag == JSON_NUMBER)
                ry = to_radians(pos->number_);
            entity3d_rotate_Y(e, ry);

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
                phys_ground_entity(clap_get_phys(scene->clap_ctx), e);

            if (c)
                c->speed  = speed;

            mat4x4_translate_in_place(e->mx, e->pos[0], e->pos[1], e->pos[2]);
            mat4x4_scale_aniso(e->mx, e->mx, e->scale, e->scale, e->scale);

            if (phys) {
                entity3d_add_physics(e, clap_get_phys(scene->clap_ctx), mass, class,
                                     ptype, geom_off, geom_radius, geom_length);
                phys_body_set_contact_params(e->phys_body,
                                             .bounce = bounce,
                                             .bounce_vel = bounce_vel);
                if (c)
                    phys_body_enable(e->phys_body, false);
            }

            if (entity_animated(e) && anis) {
                for (p = anis->children.head; p; p = p->next) {
                    if (p->tag != JSON_STRING)
                        continue;

                    model3d *m = e->txmodel->model;
                    int idx = animation_by_name(m, p->string_);

                    if (idx < 0)
                        continue;
                    free(m->anis.x[idx].name);
                    m->anis.x[idx].name = strdup(p->key);
                    if (!strcmp(p->key, "motion"))
                        m->anis.x[idx].nr_segments = motion_segments;

                    for (int i = 0; i < array_size(animation_sfx); i++)
                        if (!strcmp(p->key, animation_sfx[i].name)) {
                            m->anis.x[idx].frame_sfx = animation_sfx[i].frame_sfx;
                            break;
                        }
                }

                if (c &&
                    animation_by_name(e->txmodel->model, "start") >= 0 &&
                    animation_by_name(e->txmodel->model, "start_to_idle") >= 0) {
                        c->entity->anictl.state = UINT_MAX;
                        c->state = CS_START;
                        animation_push_by_name(e, scene, "start", true, true);
                }
            }
        }
    } else {
        struct instantiator *instor, *iter;
        entity3d *e;

        list_for_each_entry_iter(instor, iter, &scene->instor, entry) {
            if (!strcmp(txmodel_name(txm), instor->name)) {
                e = instantiate_entity(txm, instor, true, 0.5, scene);
                list_del(&instor->entry);
                free(instor);

                if (phys) {
                    entity3d_add_physics(e, clap_get_phys(scene->clap_ctx), mass, class,
                                         ptype, geom_off, geom_radius, geom_length);
                    phys_body_set_contact_params(e->phys_body,
                                                 .bounce = bounce,
                                                 .bounce_vel = bounce_vel);
                }
            }
        }
    }

    if (gd)
        gltf_free(gd);

    dbg("loaded model '%s'\n", name);

    if (scene->ls)
        loading_screen_progress(scene->ls, (float)nr_models / (float)total_models);
    nr_models++;

    return CERR_OK;
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

    vec3 center = {};
    vec3_sub(&s->light.dir[idx * 3], center, &s->light.pos[idx * 3]);
    view_update_from_frustum(&s->light.view[idx], &s->light.dir[idx * 3], &s->camera[0].view);

    return 0;
}

static void scene_onload(struct lib_handle *h, void *buf)
{
    struct scene *scene = buf;
    char         msg[256];
    JsonNode     *p, *m;

    if (h->state == RES_ERROR) {
        err("couldn't load scene %s\n", h->name);
        goto out;
    }

    scene->json_root = json_decode(h->buf);
    if (!scene->json_root) {
        err("couldn't parse '%s'\n", h->name);
        goto out;
    }

    if (!json_check(scene->json_root, msg)) {
        err("error parsing '%s': '%s'\n", h->name, msg);
        goto err;
    }

    if (scene->json_root->tag != JSON_OBJECT) {
        err("parse error in '%s'\n", h->name);
        goto err;
    }

    p = json_find_member(scene->json_root, "model");
    for (JsonNode *m = p->children.head; m; m = m->next)
        total_models++;

    for (p = scene->json_root->children.head; p; p = p->next) {
        if (!strcmp(p->key, "name")) {
            if (p->tag != JSON_STRING) {
                err("parse error in '%s'\n", h->name);
                goto err;
            }
            strncpy(scene->name, p->string_, sizeof(scene->name));
        } else if (!strcmp(p->key, "model")) {
            if (p->tag != JSON_ARRAY) {
                err("parse error in '%s'\n", h->name);
                goto err;
            }

            for (m = p->children.head; m; m = m->next) {
                model_new_from_json(scene, m);
            }
        } else if (!strcmp(p->key, "light") && p->tag == JSON_ARRAY) {
            JsonNode *light;

            for (light = p->children.head; light; light = light->next)
                scene_add_light_from_json(scene, light);
        } else if (!strcmp(p->key, "sfx") && p->tag == JSON_OBJECT) {
            JsonNode *sfx;

            for (sfx = p->children.head; sfx; sfx = sfx->next)
                sfx_add_from_json(&scene->sfxc, clap_get_sound(scene->clap_ctx), sfx);
        }
    }
    dbg("loaded scene: '%s'\n", scene->name);
out:
    scene->file_name = lib_figure_uri(h->type, h->name);
    ref_put(h);

    if (h->state == RES_LOADED)
        scene_control_next(scene);

    return;

err:
    h->state = RES_ERROR;
    ref_put(h);
    json_delete(scene->json_root);
    scene->json_root = NULL;
}

void scene_save(struct scene *scene, const char *name)
{
    LOCAL(char, buf);
    FILE *f;

    if (!scene->json_root || (!scene->file_name && !name))
        return;

    JsonNode *scene_name = json_find_member(scene->json_root, "name");
    if (!scene_name)
        json_prepend_member(scene->json_root, "name",
                            json_mkstring(scene->name));
    else
        scene_name->string_ = strdup(scene->name);

    buf = json_stringify(scene->json_root, "    ");
    if (!buf)
        return;

    if (!name)
        name = scene->file_name;

    f = fopen(name, "w+");
    if (!f)
        return;

    fwrite(buf, strlen(buf), 1, f);
    fclose(f);
#ifdef CONFIG_BROWSER
    EM_ASM(offerFileAsDownload(UTF8ToString($0), 'text/json');, name);
#endif /* CONFIG_BROWSER */
}

cerr scene_load(struct scene *scene, const char *name)
{
    if (scene->json_root)
        return CERR_ALREADY_LOADED;

    struct lib_handle *lh = lib_request(RES_ASSET, name, scene_onload, scene);

    err_on(lh->state != RES_LOADED);
    ref_put_last(lh);

    return CERR_OK;
}

void scene_done(struct scene *scene)
{
    struct instantiator *instor;
    struct character *iter, *ch;

    json_delete(scene->json_root);
    scene->json_root = NULL;

    free(scene->file_name);
    scene->file_name = NULL;

    list_for_each_entry_iter(ch, iter, &scene->characters, entry)
        ref_put_last(ch);

    while (!list_empty(&scene->instor)) {
        instor = list_first_entry(&scene->instor, struct instantiator, entry);
        list_del(&instor->entry);
        mem_free(instor);
    }

    sfx_container_clearout(&scene->sfxc);

    mq_release(&scene->mq);

    /*
     * clean up the shaders that weren't freed by model3d_drop()
     * via mq_release()
     */
    shaders_free(&scene->shaders);
}
