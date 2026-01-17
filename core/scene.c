// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include "messagebus.h"
#include "character.h"
#include "clap.h"
#include "debug_draw.h"
#include "display.h"
#include "gltf.h"
#include "interp.h"
#include "physics.h"
#include "shader.h"
#include "loading-screen.h"
#include "lut.h"
#include "model.h"
#include "scene.h"
#include "sound.h"
#include "json.h"
#include "ui-debug.h"
#include "ui-debug-fs.h"
#include "ui.h"

void scene_control_next(struct scene *s)
{
    struct character *first, *last, *prev, *current;

    current = scene_control_character(s);
    if (list_empty(&s->characters))
        return;

    prev = current;
    first = list_first_entry(&s->characters, struct character, entry);
    last = list_last_entry(&s->characters, struct character, entry);
    if (!current || current == last)
        s->control = first->entity;
    else
        s->control = list_next_entry(current, entry)->entity;

    current = s->control->priv;
    if (current == prev)
        return;

    if (prev)
        prev->camera = NULL;
    current->camera = s->camera;
    character_set_moved(current);
    s->camera->dist = 10;

    /* Stop the previous character from running */
    if (prev)
        character_stop(prev, s);

    trace("scene control at: '%s'\n", entity_name(s->control));
    dbg("scene control at: '%s'\n", entity_name(s->control));
}

bool scene_camera_follows(struct scene *s, struct character *ch)
{
    return scene_control_character(s) == ch;
}

cres(int) scene_camera_add(struct scene *s)
{
    s->camera = &s->cameras[s->nr_cameras];
    s->camera->view.main.near_plane  = 0.1;
    s->camera->view.main.far_plane   = 500.0;
    s->camera->view.fov              = to_radians(70);
    s->camera->view.proj_update      = true;
    s->camera->dist = 10;
    transform_init(&s->camera->xform);
    transform_set_updated(&s->camera->xform);
    CERR_RET(debug_draw_install(s->clap_ctx, s->camera), err_cerr(__cerr, "failed to initialize debug draw"));

    return cres_val(int, s->nr_cameras++);
}

#ifndef CONFIG_FINAL
static int input_text_callback(ImGuiInputTextCallbackData *data)
{
    return 0;
}

static void scene_lut_autoswitch(void *data)
{
    struct scene *scene = data;
    clap_context *ctx = scene->clap_ctx;
    struct list *list = clap_lut_list(ctx);
    lut *lut = clap_get_render_options(ctx)->lighting_lut;

    if (!scene->lut_autoswitch && scene->lut_timer) {
        /*
         * If the timer doesn't get re-armed, it'll get automatically
         * deleted by the clap_timer code. Clear the pointer here.
         */
        scene->lut_timer = NULL;
        return;
    }

    if (list_empty(list) || !lut)
        return;

    lut = CRES_RET(lut_next(list, lut), return);
    lut_apply(scene, lut);

    clap_timer_set(ctx, (double)scene->lut_autoswitch, scene->lut_timer, scene_lut_autoswitch, scene);
}

void scene_lut_autoswitch_set(struct scene *scene)
{
    if (scene->lut_timer) {
        clap_timer_cancel(scene->clap_ctx, scene->lut_timer);
        scene->lut_timer = NULL;
    }

    scene->lut_timer = CRES_RET(clap_timer_set(
        scene->clap_ctx, (double)scene->lut_autoswitch, scene->lut_timer,
        scene_lut_autoswitch, scene
    ), {});
}

static ui_debug_fs_dialog model_fs_dialog;

static const char * const model_picker_exts[] = {
    ".gltf",
    ".glb",
    NULL,
};

static void model_picker_accept(const char *cwd, const char *selected_name, bool selected_is_dir, __unused void *data)
{
    if (!selected_name || !*selected_name)
        return;

    char full[PATH_MAX];
    CERR_RET(path_join(full, sizeof(full), cwd, selected_name), return);

    dbg("will open '%s'\n", full);
}

static void model_picker_properties(const char *cwd, const char *sel, bool is_dir, void *data)
{
    igText("dir:\t%s\nitem:\t%s", cwd, sel);
}

static void scene_open_model_dialog(struct scene *scene)
{
    struct ui_debug_fs_config cfg = {
        .title              = "Select a GLB/glTF model",
        .modal              = true,
        .action_label       = "Open",
        .select_mode        = UI_DEBUG_FS_SELECT_FILE,
        .draw_right_panel   = model_picker_properties,
        .extensions         = model_picker_exts,
        .on_accept          = model_picker_accept,
        .data               = scene,
    };

    cerr err = ui_debug_fs_open(&model_fs_dialog, &cfg, NULL);
    if (IS_CERR(err))
        err_cerr(err, "failed to open filesystem dialog");
}

static void scene_parameters_debug(struct scene *scene, int cam_idx)
{
    debug_module *dbgm = ui_igBegin(DEBUG_SCENE_PARAMETERS, ImGuiWindowFlags_AlwaysAutoResize);
    struct camera *cam = &scene->camera[cam_idx];

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        if (igSliderFloat("near plane", &cam->view.main.near_plane, 0.1, 10.0, "%f",
            ImGuiSliderFlags_ClampOnInput))
            cam->view.proj_update = true;

        if (igSliderFloat("far plane", &cam->view.main.far_plane, 10.0, 1000.0, "%f",
            ImGuiSliderFlags_ClampOnInput))
            cam->view.proj_update = true;

        float fov = to_degrees(cam->view.fov);
        if (igSliderFloat("FOV", &fov, 30.0, 120.0, "%f", ImGuiSliderFlags_ClampOnInput)) {
            cam->view.fov = to_radians(fov);
            cam->view.proj_update = true;
        }

        if (igButton("Detach camera", (ImVec2){}))  scene->control = NULL;

        luts_debug(scene);

        if (igSliderInt("lut autoswitch", &scene->lut_autoswitch, 0, 60, "%d", 0))
            scene_lut_autoswitch_set(scene);

        render_options *ropts = clap_get_render_options(scene->clap_ctx);
        igCheckbox("shadow outline", &ropts->shadow_outline);
        if (ropts->shadow_outline)
            igSliderFloat("shadow outline threshold", &ropts->shadow_outline_threshold,
                          0.0, 1.0, "%.02f", ImGuiSliderFlags_ClampOnInput);
        igCheckbox("VSM shadows", &ropts->shadow_vsm);
        igCheckbox("shadow msaa", &ropts->shadow_msaa);
        igCheckbox("model msaa", &ropts->model_msaa);
        igCheckbox("edge sobel", &ropts->edge_sobel);
        igCheckbox("edge antialiasing", &ropts->edge_antialiasing);
        if (!ropts->edge_sobel) {
            igText("Laplace kernel size");
            igSameLine(0.0, 0.0);
            igRadioButton_IntPtr("3x3", &ropts->laplace_kernel, 3);
            igSameLine(0.0, 0.0);
            igRadioButton_IntPtr("5x5", &ropts->laplace_kernel, 5);
        }
        igCheckbox("overlay draws", &ropts->overlay_draws_enabled);
        if (igCheckbox("debug draws", &ropts->debug_draws_enabled))
            phys_capsules_debug_enable(clap_get_phys(scene->clap_ctx), ropts->debug_draws_enabled);
        if (igCheckbox("collision draws", &ropts->collision_draws_enabled))
            phys_contacts_debug_enable(clap_get_phys(scene->clap_ctx), ropts->collision_draws_enabled);
        if (igCheckbox("velocity draws", &ropts->velocity_draws_enabled))
            phys_velocities_debug_enable(clap_get_phys(scene->clap_ctx), ropts->velocity_draws_enabled);
        igCheckbox("camera frusta draws", &ropts->camera_frusta_draws_enabled);
        igCheckbox("light frusta draws", &ropts->light_frusta_draws_enabled);
        igCheckbox("aabb draws", &ropts->aabb_draws_enabled);
        igCheckbox("use SSAO", &ropts->ssao);
        if (ropts->ssao) {
            igSliderFloat("SSAO radius", &ropts->ssao_radius, 0.1, 2.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
            igSliderFloat("SSAO weight", &ropts->ssao_weight, 0.0, 1.0, "%.4f", ImGuiSliderFlags_ClampOnInput);
        }
        igCheckbox("use HDR", &ropts->hdr);
        igSliderFloat("bloom exposure", &ropts->bloom_exposure, 0.01, 5.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
        igSliderFloat("bloom intensity", &ropts->bloom_intensity, 0.1, 10.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
        igSliderFloat("bloom threshold", &ropts->bloom_threshold, 0.01, 1.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
        int bop = (int)ropts->bloom_operator;
        igText("bloom tonemapping op:");
        igSameLine(0.0, 0.0);
        igPushID_Str("bop");
        igRadioButton_IntPtr("Reinhard", &bop, 0);
        igSameLine(0.0, 0.0);
        igRadioButton_IntPtr("ACES", &bop, 1);
        igPopID();
        ropts->bloom_operator = (float)bop;
        igSliderFloat("lighting exposure", &ropts->lighting_exposure, 0.1, 10.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
        int lop = (int)ropts->lighting_operator;
        igText("lighting tonemapping op:");
        igSameLine(0.0, 0.0);
        igPushID_Str("lop");
        igRadioButton_IntPtr("Reinhard", &lop, 0);
        igSameLine(0.0, 0.0);
        igRadioButton_IntPtr("ACES", &lop, 1);
        igPopID();
        ropts->lighting_operator = (float)lop;
        igSliderFloat("contrast", &ropts->contrast, 0.01, 1.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
        igSeparator();
        if (igButton("disable fog", (ImVec2){})) {
            ropts->fog_near = scene->camera->view.main.far_plane;
            ropts->fog_far = scene->camera->view.main.far_plane;
        }
        igDragFloatRange2(
            "fog near/far",
            &ropts->fog_near,
            &ropts->fog_far,
            1.0, 1.0, scene->camera->view.main.far_plane,
            "near: %.02f", "far: %.02f", 0
        );
        igColorEdit3(
            "fog_color",
            ropts->fog_color,
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_NoLabel  |
            ImGuiColorEditFlags_NoTooltip
        );
        igSameLine(0, 0);
        igText("fog color");
        igSeparator();
        igInputText("scene name", scene->name, sizeof(scene->name), ImGuiInputFlags_Tooltip,
                    input_text_callback, NULL);
        if (igButton("save level", (ImVec2){}))
            scene_save(scene, NULL);
    }

    ui_igEnd(DEBUG_SCENE_PARAMETERS);
}

static void light_debug(struct scene *scene)
{
    debug_module *dbgm = ui_igBegin(DEBUG_LIGHT, 0);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        ui_igControlTableHeader("ambient light", "color");
        ui_igColorEdit3(
            "color",
            scene->light.ambient,
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_NoLabel  |
            ImGuiColorEditFlags_NoTooltip
        );
        igEndTable();

        ui_igControlTableHeader("shadow tint", "color");
        ui_igColorEdit3(
            "color",
            scene->light.shadow_tint,
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_NoLabel  |
            ImGuiColorEditFlags_NoTooltip
        );
        igEndTable();

        for (int idx = 0; idx < scene->light.nr_lights; idx++) {
            igPushID_Int(idx);
            ui_igControlTableHeader("light %d", "pos", idx);

            ui_igCheckbox("directional", (bool *)&scene->light.is_dir[idx]);
            if (ui_igSliderFloat3("pos", &scene->light.pos[3 * idx], -500, 500, "%.02f", 0) &&
                scene->light.is_dir[idx])
                vec3_sub(&scene->light.dir[3 * idx], (vec3){}, &scene->light.pos[3 * idx]);
            if (ui_igSliderFloat3("dir", &scene->light.dir[3 * idx], -500, 500, "%.02f", 0) &&
                scene->light.is_dir[idx])
                vec3_sub(&scene->light.pos[3 * idx], (vec3){}, &scene->light.dir[3 * idx]);
            ui_igSliderFloat3("att", &scene->light.attenuation[3 * idx], 0.0001, 10.0, "%.04f", 0);

            ui_igColorEdit3(
                "color",
                &scene->light.color[3 * idx],
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_NoLabel  |
                ImGuiColorEditFlags_NoTooltip
            );

            igEndTable();
            igPopID();
        }
    }

    ui_igEnd(DEBUG_LIGHT);
}

static void scene_characters_debug(struct scene *scene)
{
    debug_module *dbgm = ui_igBegin(DEBUG_CHARACTERS, 0);
    struct character *c;

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        list_for_each_entry(c, &scene->characters, entry) {
            const char *name = entity_name(c->entity);

            ui_igControlTableHeader("character '%s'", "jump forward", name);
            igPushID_Ptr(c);
            ui_igSliderFloat("jump forward", &c->jump_forward, 0.1, 10.0, "%f", ImGuiSliderFlags_AlwaysClamp);
            ui_igSliderFloat("jump upward", &c->jump_upward, 0.1, 10.0, "%f", ImGuiSliderFlags_AlwaysClamp);
            ui_igSliderFloat("speed", &c->speed, 0.1, 10.0, "%f", ImGuiSliderFlags_AlwaysClamp);
            ui_igCheckbox("can jump", &c->can_jump);
            ui_igCheckbox("can dash", &c->can_dash);
            igPopID();
            igEndTable();
        }
    }

    ui_igEnd(DEBUG_CHARACTERS);
}

static void model_picker(struct scene *scene)
{
    entity_inspector *ei = &scene->entity_inspector;

    ui_igControlTableHeader("model", "model");

    if (ui_igBeginCombo("model", ei->entity ? txmodel_name(ei->entity->txmodel) : "<none>",
                        ImGuiComboFlags_HeightLargest)) {
        model3dtx *txm;
        list_for_each_entry(txm, &scene->mq.txmodels, entry) {
            bool selected = false;

            if (ei->entity->txmodel == txm)
                selected = true;

            igPushID_Ptr(txm);
            if (igSelectable_Bool(txmodel_name(txm), selected, selected ? ImGuiSelectableFlags_Highlight : 0, (ImVec2){0, 0})) {
                igSetItemDefaultFocus();

                /*
                 * list_empty(&txm->entities) should never be empty:
                 * removing the last entity should remove the txmodel
                 */
                if (list_empty(&txm->entities)) {
                    if (igBeginErrorTooltip()) {
                        igText("model '%s' has no entities", txmodel_name(txm));
                        igEndTooltip();
                    }
                    err("model '%s' has no entities", txmodel_name(txm));
                    igPopID();
                    continue;
                }

                ei->entity = list_first_entry(&txm->entities, entity3d, entry);

                if (ei->switch_control) {
                    scene->control = ei->entity;
                    transform_set_updated(&scene->camera->xform);
                }
            }
            igPopID();
        }

        ui_igEndCombo();
    }

    ui_igCheckbox("switch scene control", &ei->switch_control);
    ui_igHelpTooltip(
            "Switch scene control to the selected model / entity. "
            "You can move the entity with motion controls only"
            "when it's the controlled entity"
    );

    ui_igCheckbox("follow scene control", &ei->follow_control);
    ui_igHelpTooltip("Automatically switched to the control entity");

    igEndTable();
}

static const char *attr_names[ATTR_MAX] = {
    [ATTR_POSITION] = "vertex",
    [ATTR_TEX]      = "UV",
    [ATTR_NORMAL]   = "normals",
    [ATTR_TANGENT]  = "tangents",
    [ATTR_JOINTS]   = "joints",
    [ATTR_WEIGHTS]  = "joint weights",
};

static void model_joint_subtree(model3d *m, unsigned int idx, ImGuiTreeNodeFlags flags)
{
    auto joint = &m->joints[idx];
    if (!igTreeNodeEx_StrStr(joint->name, flags, "%s (id: %d/%u)", joint->name, joint->id, idx))
        return;

    int *child;
    darray_for_each(child, joint->children)
        model_joint_subtree(m, *child, flags);

    igTreePop();
}

static void entity_joint_subtree(entity3d *e, unsigned int idx, ImGuiTreeNodeFlags flags)
{
    auto m = e->txmodel->model;
    auto joint = &m->joints[idx];
    if (!igTreeNodeEx_StrStr(joint->name, flags, "%s (id: %d/%u)", joint->name, joint->id, idx))
        return;

    ui_igTableHeader("joint TRS", (const char *[]) { "transform", "X", "Y", "Z", "W "}, 5);
    ui_igVecRow(e->joints[idx].translation, 3, "translation");
    ui_igVecRow(e->joints[idx].rotation, 4, "rotation");
    igEndTable();
    ui_igMat4x4(e->joint_transforms[idx], joint->name);

    int *child;
    darray_for_each(child, joint->children)
        entity_joint_subtree(e, *child, flags);

    igTreePop();
}

static void model_tabs(model3dtx *txm)
{
    if (!igBeginTabBar("model properties", 0))
        return;

    model3d *m = txm->model;

    if (igBeginTabItem("buffers", NULL, 0)) {
        buffer_debug_header();

        for (int i = 0; i < m->nr_lods; i++)
            if (buffer_loaded(&m->index[i]))
                buffer_debug(&m->index[i], "index");
        for (enum shader_vars v = 0; v < ATTR_MAX; v++) {
            if (buffer_loaded(&m->attr[v]))
                buffer_debug(&m->attr[v], attr_names[v]);
        }

        igEndTable();
        igEndTabItem();
    }

    if (igBeginTabItem("LODs", NULL, 0)) {
        igSeparatorText("LODs");
        igText("vertices: %u", m->nr_vertices);
        ui_igTableHeader("lod", (const char *[]){ "LOD", "faces", "edges", "error" }, 4);
        for (int i = 0; i < m->nr_lods; i++) {
            igTableNextRow(0, 0);
            igTableNextColumn();
            igText("%d", i);
            igTableNextColumn();
            igText("%u", m->nr_faces[i]);
            igTableNextColumn();
            igText("%u", m->nr_faces[i] * 3);
            igTableNextColumn();
            igText("%f", m->lod_errors[i]);
        }
        igEndTable();
        igEndTabItem();
    }

    if (igBeginTabItem("textures", NULL, 0)) {
        texture_debug_header();

        for (enum shader_vars v = ATTR_MAX; v < UNIFORM_TEX_MAX; v++) {
            auto tex = CRES_RET(model3dtx_loaded_texture(txm, v), continue);
            texture_debug(tex, shader_get_var_name(v));
        }

        igEndTable();
        igEndTabItem();
    }

    if (m->nr_joints) {
        if (igBeginTabItem("joints", NULL, 0)) {
            if (igBeginChild_Str("joints", (ImVec2){}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, 0)) {
                /*
                 * XXX: ImGuiTreeNodeFlags_DrawLinesToNodes looks nicer, but
                 * requires fixing on the ImGui sidea
                 */
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_DefaultOpen;
                model_joint_subtree(m, m->root_joint, flags);
            }
            igEndChild();
            igEndTabItem();
        }
    }

    if (igBeginTabItem("material", NULL, 0)) {
        ui_igControlTableHeader("material", "roughness");
        material *mat = &txm->mat;
        bool noisy_roughness = mat->roughness_oct > 0;
        if (ui_igCheckbox("noisy roughness", &noisy_roughness))
            mat->roughness_oct = noisy_roughness ? 1 : 0;

        if (noisy_roughness) {
            igPushID_Str("roughness noise");
            ui_igSliderFloat("-> scale", &mat->roughness_scale, 0.0, 100.0, "%.02f", 0);
            ui_igSliderFloat("-> floor", &mat->roughness, 0.0, 1.0, "%.04f", 0);
            ui_igSliderFloat("-> ceil", &mat->roughness_ceil, 0.0, 1.0, "%.04f", 0);
            ui_igSliderFloat("-> amp", &mat->roughness_amp, 0.0, 1.0, "%.04f", 0);
            ui_igSliderInt("-> oct", &mat->roughness_oct, 1, 10, "%d", 0);
            igPopID();
        } else {
            ui_igSliderFloat("roughness", &mat->roughness, 0.0, 1.0, "%.04f", 0);
            mat->metallic_mode = MAT_METALLIC_INDEPENDENT;
        }

        bool noisy_metallic = mat->metallic_oct > 0;
        if (ui_igCheckbox("noisy metallic", &noisy_metallic))
            mat->metallic_oct = noisy_metallic ? 1 : 0;

        if (noisy_metallic) {
            igPushID_Str("metallic noise");
            if (noisy_roughness)
                ui_igCheckbox("shared scale", &mat->shared_scale);
            else
                mat->shared_scale = false;

            if (!mat->shared_scale)
                ui_igSliderFloat("-> scale", &mat->metallic_scale, 0.0, 100.0, "%.02f", 0);
            ui_igSliderFloat("-> floor", &mat->metallic, 0.0, 1.0, "%.04f", 0);
            ui_igSliderFloat("-> ceil", &mat->metallic_ceil, 0.0, 1.0, "%.04f", 0);

            ui_igLabel("mode");
            igTableNextColumn();
            igRadioButton_IntPtr("independent", &mat->metallic_mode, MAT_METALLIC_INDEPENDENT);
            if (noisy_roughness) {
                igSameLine(0.0, 4.0);
                igRadioButton_IntPtr("roughness", &mat->metallic_mode, MAT_METALLIC_ROUGHNESS);
                igSameLine(0.0, 4.0);
                igRadioButton_IntPtr("1-roughness", &mat->metallic_mode, MAT_METALLIC_ONE_MINUS_ROUGHNESS);
            }

            if (mat->metallic_mode == MAT_METALLIC_INDEPENDENT) {
                ui_igSliderFloat("-> amp", &mat->metallic_amp, 0.0, 1.0, "%.04f", 0);
                ui_igSliderInt("-> oct", &mat->metallic_oct, 1, 10, "%d", 0);
            }
            igPopID();
        } else {
            ui_igSliderFloat("metallic", &mat->metallic, 0.0, 1.0, "%.04f", 0);
        }
        igEndTable();
        igEndTabItem();
    }

    igEndTabBar();
    igSeparator();
}

static void scene_entity_inspector_debug(struct scene *scene)
{
    entity_inspector *ei = &scene->entity_inspector;
    if (!ei->entity || ei->follow_control)
        ei->entity = scene->control;

    debug_module *dbgm = ui_igBegin_name(
        DEBUG_ENTITY_INSPECTOR,
        0,
        "entity '%s'",
        entity_name(ei->entity)
    );

    ui_debug_fs_draw(&model_fs_dialog);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        /*
         * Stretch all following widgets horizontally to fill the window,
         * unless told otherwis.
         */
        igPushItemWidth(-1.0);

        model_picker(scene);

        if (igButton("browse glTF...", (ImVec2){}))
            scene_open_model_dialog(scene);

        if (!ei->entity) {
            igPopItemWidth();
            goto out;
        }

        /* Hold the txm reference for the remainder of the function */
        model3dtx *txm = ref_get(ei->entity->txmodel);

        model_tabs(txm);

        ui_igControlTableHeader("entity", "bloom thr");

        if (ui_igBeginCombo("entity", entity_name(ei->entity), ImGuiComboFlags_HeightLargest)) {
            entity3d *e;
            list_for_each_entry(e, &txm->entities, entry) {
                if (!entity3d_matches(e, ENTITY3D_ALIVE))
                    continue;

                bool selected = ei->entity == e;

                igPushID_Ptr(e);
                if (igSelectable_Bool(entity_name(e), selected, selected ? ImGuiSelectableFlags_Highlight : 0, (ImVec2){0, 0})) {
                    igSetItemDefaultFocus();

                    ei->entity = e;
                    if (ei->switch_control) {
                        scene->control = e;
                        transform_set_updated(&scene->camera->xform);
                    }
                }
                igPopID();
            }
            ui_igEndCombo();
        }

        ui_igLabel("actions");
        igTableNextColumn();

        entity3d *e = ei->entity;
        if (igButton("delete", (ImVec2){})) {
            if (e == scene->control)
                scene_control_next(scene);

            entity3d_delete(e);

            /*
             * entity3d_delete() doesn't delete txm as well, because we're
             * holding a reference to it, so dereferencing it is safe
             */
            if (!list_empty(&txm->entities)) {
                ei->entity = e = list_first_entry(&txm->entities, entity3d, entry);
            } else {
                /*
                 * Switching to a different model, it's safe to drop the
                 * reference here
                 */
                ei->entity = e = scene->control;
                ref_put(txm);

                /* Get a reference to the new txmodel */
                txm = ref_get(e->txmodel);
            }
        }

        igSameLine(0.0, 4.0);

        if (igButton("terrain clamp", (ImVec2){}))
            phys_ground_entity(clap_get_phys(scene->clap_ctx), e);

        ui_igCheckbox("skip shadow", &txm->model->skip_shadow);

        static bool draw_armature;

        ui_igCheckbox("draw armature", &draw_armature);

        if (draw_armature) {
            auto m = e->txmodel->model;
            static int hl = -1;

            if (ui_igBeginCombo("joint", hl >= 0 ? m->joints[hl].name : "<none>", ImGuiComboFlags_HeightLargest)) {
                for (unsigned int j = 0; j < m->nr_joints; j++) {
                    bool selected = hl == j;

                    igPushID_Int(j);
                    if (igSelectable_Bool(m->joints[j].name, selected, selected ? ImGuiSelectableFlags_Highlight : 0, (ImVec2){0, 0})) {
                        igSetItemDefaultFocus();
                        hl = j;
                    }
                    igPopID();
                }
                igEndCombo();
            }

            for (unsigned i = 0; i < m->nr_joints; i++) {
                message_send(scene->clap_ctx, &(struct message) {
                    .type   = MT_DEBUG_DRAW,
                    .debug_draw     = (struct message_debug_draw) {
                        .color      = { hl == i ? 1.0 : 0.0, hl == i ? 0.0 : 1.0, 0.0, 1.0 },
                        .radius     = hl == i ? 8.0 : 2.0,
                        .shape      = DEBUG_DRAW_DISC,
                        .v0         = { e->joints[i].pos[0], e->joints[i].pos[1], e->joints[i].pos[2] },
                    }
                });

                int *j;
                darray_for_each(j, m->joints[i].children)
                    message_send(scene->clap_ctx, &(struct message) {
                        .type   = MT_DEBUG_DRAW,
                        .debug_draw     = (struct message_debug_draw) {
                            .color      = { 0.5, 0.5, 0.0, 1.0 },
                            .thickness  = 2.0,
                            .shape      = DEBUG_DRAW_LINE,
                            .v0         = { e->joints[i].pos[0], e->joints[i].pos[1], e->joints[i].pos[2] },
                            .v1         = { e->joints[*j].pos[0], e->joints[*j].pos[1], e->joints[*j].pos[2] },
                        }
                    });
            }
        }

        static bool draw_aabb;

        ui_igCheckbox("draw aabb", &draw_aabb);
        if (draw_aabb) {
            struct message dm_aabb = {
                .type   = MT_DEBUG_DRAW,
                .debug_draw     = (struct message_debug_draw) {
                    .color      = { 1.0, 0.0, 0.0, 1.0 },
                    .thickness  = 4.0,
                    .shape      = DEBUG_DRAW_AABB,
                }
            };
            entity3d_aabb_min(e, dm_aabb.debug_draw.v0);
            entity3d_aabb_max(e, dm_aabb.debug_draw.v1);
            message_send(scene->clap_ctx, &dm_aabb);

            struct message dm_aabb_center = {
                .type   = MT_DEBUG_DRAW,
                .debug_draw     = (struct message_debug_draw) {
                    .color      = { 1.0, 0.0, 0.0, 1.0 },
                    .radius     = 10.0,
                    .shape      = DEBUG_DRAW_DISC,
                    .v0         = { e->aabb_center[0], e->aabb_center[1], e->aabb_center[2] },
                }
            };
            message_send(scene->clap_ctx, &dm_aabb_center);
        }
        ui_igCheckbox("outline exclude", &e->outline_exclude);

        ui_igCheckbox("visible", (bool *)&e->visible);

        vec3 pos;
        transform_pos(&e->xform, pos);

        ui_igLabel("pos");
        igTableNextColumn();
        auto moved = igDragFloat3(
            "##pos",
            pos,
            0.1, -500.0, 500.0,
            "%.02f", 0
        );

        if (moved) {
            entity3d_position(e, pos);
            transform_set_updated(&scene->camera->xform);
        }

        int rotated = 0;
        float angles[3];
        transform_rotation(&e->xform, angles, true);
        if (ui_igSliderFloat("rx", &angles[0], -180.0, 180.0, "%.02f", 0))
            rotated++;

        if (ui_igSliderFloat("ry", &angles[1], -180.0, 180.0, "%.02f", 0))
            rotated++;

        if (ui_igSliderFloat("rz", &angles[2], -180.0, 180.0, "%.02f", 0))
            rotated++;

        if (rotated)
            transform_set_angles(&e->xform, angles, true);

        ui_igSliderFloat("bloom thr", &e->bloom_threshold, 0.0, 1.0, "%.02f", 0);
        ui_igSliderFloat("bloom int", &e->bloom_intensity, -10.0, 10.0, "%.04f", 0);

        int lod = e->cur_lod;
        int nr_lods = max(txm->model->nr_lods - 1, 0);
        if (ui_igSliderInt("LOD", &lod, 0, nr_lods, "%u", 0))
            entity3d_set_lod(e, lod, true);
        igEndTable();

        if (txm->model->nr_joints) {
            if (igBeginChild_Str("joints", (ImVec2){}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, 0)) {
                /*
                 * XXX: ImGuiTreeNodeFlags_DrawLinesToNodes looks nicer, but
                 * requires fixing on the ImGui sidea
                 */
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DrawLinesFull;
                entity_joint_subtree(e, txm->model->root_joint, flags);
            }
            igEndChild();
        }

        igPopItemWidth();

        ref_put(txm);
    }

out:
    ui_igEnd(DEBUG_ENTITY_INSPECTOR);
}

static void scene_debug_frusta(struct scene *scene, struct view *view)
{
    static const uint8_t frustum_edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, // near plane edges
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, // far plane edges
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }  // connecting edges
    };

    static const vec4 color_for_cascade[] = {
        { 1.0, 0.0, 0.0, 1.0 },
        { 0.0, 1.0, 0.0, 1.0 },
        { 0.0, 0.0, 1.0, 1.0 },
        { 1.0, 1.0, 0.0, 1.0 },
    };

    for (int v = 0; v < CASCADES_MAX; v++) {
        struct subview *src = &view->debug_subview[v];
        if (view->subview[v].debug_hide)    continue;

        for (int i = 0; i < 12; ++i) {
            struct message dm = {
                .type       = MT_DEBUG_DRAW,
                .debug_draw = (struct message_debug_draw) {
                    .shape = DEBUG_DRAW_LINE,
                    .thickness = 2.0f
                }
            };
            vec3_dup(dm.debug_draw.v0, src->frustum_corners[frustum_edges[i][0]]);
            vec3_dup(dm.debug_draw.v1, src->frustum_corners[frustum_edges[i][1]]);
            vec4_dup(dm.debug_draw.color, color_for_cascade[v]);
            message_send(scene->clap_ctx, &dm);
        }
    }
}

#else
static inline void scene_parameters_debug(struct scene *scene, int cam_idx) {}
static inline void light_debug(struct scene *scene) {}
static inline void scene_characters_debug(struct scene *scene) {}
static inline void scene_entity_inspector_debug(struct scene *scene) {}
static inline void scene_debug_frusta(struct scene *scene, struct view *view) {}
#endif /* CONFIG_FINAL */

static void scene_camera_calc(struct scene *s, int camera)
{
    struct camera *cam = &s->cameras[camera];
    view_update_perspective_projection(&cam->view, s->width, s->height,
                                       cam->zoom ? 0.5 : 1.0);

    camera_update(s->camera, s);

    view_update_from_angles(&cam->view, &cam->xform);
    view_calc_frustum(&cam->view);

    entity3d *env = cam->bv;
    cam->bv = NULL;

    float near_backup = 0.0;
    if (env) {
        vec3 light_dir;
        /*
         * Calculate frusta's near plane extension from the bounding volume's
         * dimensions and the light direction vector.
         * TODO: this does not take into account the camera's position within
         * the AABB.
         */
        vec3_norm_safe(light_dir, &s->light.dir[0]);
        float xz_mix = linf_interp(
            entity3d_aabb_Z(env),
            entity3d_aabb_X(env),
            fabsf(vec3_mul_inner(light_dir, (vec3){ 1.0, 0.0, 0.0 }))
        );
        // near_backup = linf_interp(
        //     xz_mix,
        //     entity3d_aabb_Y(env),
        //     fabsf(vec3_mul_inner(light_dir, (vec3){ 0.0, 1.0, 0.0 }))
        // );
        float ycos = fmaxf(fabsf(vec3_mul_inner(light_dir, (vec3){ 0.0, 1.0, 0.0 })), 0.2);
        near_backup = min(xz_mix / ycos, entity3d_aabb_avg_edge(env));
    }
    /* only the first light source get to cast shadows for now */
    bool shadow_vsm = clap_get_render_options(s->clap_ctx)->shadow_vsm;
    view_update_from_frustum(&s->light.view[0], &cam->view, &s->light.dir[0 * 3], near_backup, !shadow_vsm);
    view_calc_frustum(&s->light.view[0]);
}

void scene_cameras_calc(struct scene *s)
{
    int i;

    for (i = 0; i < s->nr_cameras; i++)
        scene_camera_calc(s, i);
}

void scene_characters_move(struct scene *s)
{
    struct character *ch, *current = scene_control_character(s);
    float lin_speed;

    if (current) {
        lin_speed = current->lin_speed;
    } else {
        double dt = clap_get_fps_delta(s->clap_ctx).tv_nsec / (double)NSEC_PER_SEC;
        lin_speed = s->lin_speed * dt;
    }

    /* Always compute the active inputs in the frame */
    motion_compute(&s->mctl, s->camera, lin_speed);

    if (!current) {
        entity3d_move(s->control, (vec3){ s->mctl.dx, 0.0, s->mctl.dz });
        transform_set_updated(&s->camera->xform);
        return;
    }

    list_for_each_entry(ch, &s->characters, entry) {
        /* But only apply them to the active character */
        if (current == ch)
            character_move(ch, s);
    }
}

static int scene_handle_input(struct clap_context *ctx, struct message *m, void *data)
{
    struct scene *s = data;

#ifndef CONFIG_FINAL
    if (m->input.debug_action || (m->input.pad_lb && m->input.pad_rb)) {
        debug_camera_action(s->camera);

        struct view *cam_view = &s->camera->view;
        memcpy(cam_view->debug_subview, cam_view->subview, sizeof(cam_view->debug_subview));

        struct view *light_view = &s->light.view[0];
        memcpy(light_view->debug_subview, light_view->subview, sizeof(light_view->debug_subview));
    }
#endif /* CONFIG_FINAL */
    if (m->input.exit)
        display_request_exit();
#ifndef CONFIG_FINAL
    if (m->input.tab || m->input.stick_r)
        scene_control_next(s);
#endif
    if (m->input.resize) {
        s->width = m->input.x;
        s->height = m->input.y;
        if (s->camera)
            s->camera->view.proj_update = true;
    }
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
        message_send(ctx, &m);
    }
#endif
    if (clap_is_paused(s->clap_ctx))    return 0;

    if (m->input.zoom == 1) {
        if (!s->camera->zoom)
            s->camera->view.proj_update = true;
        s->camera->zoom = 1;
    } else if (m->input.zoom == 2) {
        if (s->camera->zoom)
            s->camera->view.proj_update = true;
        s->camera->zoom = 0;
    }

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

void scene_update(struct scene *scene)
{
    scene_parameters_debug(scene, 0);
    scene_characters_debug(scene);
    scene_entity_inspector_debug(scene);
    light_debug(scene);

    mq_update(&scene->mq);

    if (scene->mctl.rs_dy) {
        float delta = scene->mctl.rs_dy * scene->ang_speed;

        camera_add_pitch(scene->camera, delta);
        transform_set_updated(&scene->camera->xform);
    }
    if (scene->mctl.rs_dx) {
        /* XXX: need a better way to represend horizontal rotational speed */
        camera_add_yaw(scene->camera, scene->mctl.rs_dx * scene->ang_speed * 1.5);
        transform_set_updated(&scene->camera->xform);
    }

    clap_context *ctx = scene->clap_ctx;
    camera_move(scene->camera, clap_get_fps_fine(ctx));

    if (clap_get_render_options(ctx)->camera_frusta_draws_enabled)
        scene_debug_frusta(scene, &scene->camera->view);
    if (clap_get_render_options(ctx)->light_frusta_draws_enabled)
        scene_debug_frusta(scene, &scene->light.view[0]);

    motion_reset(&scene->mctl, scene);
}

cerr scene_init(struct scene *scene, struct clap_context *ctx)
{
    memset(scene, 0, sizeof(*scene));
    scene->clap_ctx = ctx;
    scene->auto_yoffset = 4.0;
    mq_init(&scene->mq, scene);
    list_init(&scene->characters);
    list_init(&scene->instor);
    sfx_container_init(&scene->sfxc);

    int i;
    for (i = 0; i < LIGHTS_MAX; i++) {
        float attenuation[3] = { 1, 0, 0 };
        light_set_attenuation(&scene->light, i, attenuation);
        light_set_directional(&scene->light, i, true);
    }
    light_set_ambient(&scene->light, (float[]){ 0.1, 0.1, 0.1 });
    light_set_shadow_tint(&scene->light, (float[]){ 0.1, 0.1, 0.1 });

    /* messagebus_done() will "unsubscribe" (free) these */
    CERR_RET_CERR(subscribe(ctx, MT_INPUT, scene_handle_input, scene));

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

    CRES_RET_CERR(sfx_new(sfxc, _sfx->key, _sfx->string_, ctx));

    return CERR_OK;
}

unsigned int total_models, nr_models;

static cerr model_new_from_json(struct scene *scene, JsonNode *node)
{
    double mass = 1.0, bounce = 0.2, bounce_vel = 0.2, geom_off = 0.0, geom_radius = 1.0;
    double geom_length = 1.0, speed = 0.75, roughness = -1.0, metallic = -1.0;
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
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "metallic"))
            metallic = p->number_;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "roughness"))
            roughness = p->number_;
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
                    CERR_RET(
                        gltf_instantiate_one(gd, i),
                        { gltf_free(gd); return __cerr; }
                    );
                    break; /* XXX: why? */
                }
        } else {
            CERR_RET(
                gltf_instantiate_one(gd, root),
                { gltf_free(gd); return __cerr; }
            );
        }

        /* In the absence of a dedicated collision mesh, use the main one */
        if (collision < 0)
            collision = root ? root : 0;
    } else {
        CERR_RET(
            gltf_instantiate_one(gd, 0),
            { gltf_free(gd); return __cerr; }
        );
        collision = 0;
    }
    txm = mq_model_last(&scene->mq);
    txm->model->cull_face = cull_face;
    txm->model->alpha_blend = alpha_blend;
    if (roughness >= 0.0)
        txm->mat.roughness = clampd(roughness, 0.0, 1.0);
    if (metallic >= 0.0)
        txm->mat.metallic = clampd(metallic, 0.0, 1.0);

    model3d_set_name(txm->model, name);

    txm->model->sfxc.on_add = scene->sfxc.on_add;
    txm->model->sfxc.data = scene->sfxc.data;

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

            jpos = json_find_member(it, "name");
            if (jpos && jpos->tag == JSON_STRING)
                e->name = strdup(jpos->string_);

            jpos = json_find_member(it, "position");
            if (jpos->tag != JSON_ARRAY)
                continue;

            vec3 e_pos = {};

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
                entity3d_rotate(e, 0, to_radians(pos->number_), 0);

            double _light_color[3];
            jpos = json_find_member(it, "light_color");
            if (jpos && jpos->tag == JSON_ARRAY) {
                e->light_idx = CRES_RET(light_get(&scene->light), goto light_done);

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
                    light_set_directional(&scene->light, e->light_idx, false);
                }
            }

light_done:
            jpos = json_find_member(it, "bloom_intensity");
            if (jpos && jpos->tag == JSON_NUMBER)
                e->bloom_intensity = jpos->number_;

            jpos = json_find_member(it, "bloom_threshold");
            if (jpos && jpos->tag == JSON_NUMBER)
                e->bloom_threshold = jpos->number_;

            if (terrain_clamp)
                phys_ground_entity(clap_get_phys(scene->clap_ctx), e);

            if (c)
                c->speed  = speed;

            transform_translate_mat4x4(&e->xform, e->mx);
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

static cerr scene_add_light_from_json(struct scene *s, JsonNode *light)
{
    if (light->tag != JSON_OBJECT)
        return CERR_INVALID_FORMAT;

    JsonNode *jambient = json_find_member(light, "ambient_color");
    if (jambient) {
        if (jambient->tag != JSON_ARRAY)
            return CERR_INVALID_FORMAT;

        double color[3];
        if (json_double_array(jambient, color, 3))
            return CERR_INVALID_FORMAT;

        light_set_ambient(&s->light, (float[]){ color[0], color[1], color[2] });

        return CERR_OK;
    }

    JsonNode *jtint = json_find_member(light, "shadow_tint");
    if (jtint) {
        if (jtint->tag != JSON_ARRAY)
            return CERR_INVALID_FORMAT;

        double color[3];
        if (json_double_array(jtint, color, 3))
            return CERR_INVALID_FORMAT;

        light_set_shadow_tint(&s->light, (float[]){ color[0], color[1], color[2] });

        return CERR_OK;
    }

    JsonNode *jpos = json_find_member(light, "position");
    JsonNode *jcolor = json_find_member(light, "color");
    if (!jpos || jpos->tag != JSON_ARRAY || !jcolor || jcolor->tag != JSON_ARRAY)
        return CERR_INVALID_FORMAT;

    double pos[3], color[3];
    if (json_double_array(jpos, pos, 3) ||
        json_double_array(jcolor, color, 3))
        return CERR_INVALID_FORMAT;

    int idx = CRES_RET_CERR(light_get(&s->light));

    float fpos[3] = { pos[0], pos[1], pos[2] };
    float fcolor[3] = { color[0], color[1], color[2] };
    light_set_pos(&s->light, idx, fpos);
    light_set_color(&s->light, idx, fcolor);

    bool shadow_vsm = clap_get_render_options(s->clap_ctx)->shadow_vsm;
    vec3 center = {};
    vec3_sub(&s->light.dir[idx * 3], center, &s->light.pos[idx * 3]);
    view_update_from_frustum(&s->light.view[idx], &s->camera[0].view, &s->light.dir[idx * 3], 0.0, !shadow_vsm);

    return CERR_OK;
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
                CERR_RET(model_new_from_json(scene, m), goto err);
            }
        } else if (!strcmp(p->key, "light") && p->tag == JSON_ARRAY) {
            JsonNode *light;

            for (light = p->children.head; light; light = light->next)
                CERR_RET(scene_add_light_from_json(scene, light), goto err);
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

    cerr ret = CERR_OK;
    if (lh->state != RES_LOADED)
        ret = CERR_SCENE_NOT_LOADED;
    ref_put_last(lh);

    return ret;
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
}
