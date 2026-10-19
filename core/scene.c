// SPDX-License-Identifier: Apache-2.0
#include "error.h"
#include <limits.h>
#include "messagebus.h"
#include "character.h"
#include "clap.h"
#include "debug_draw.h"
#include "display.h"
#include "gltf.h"
#include "graphics-settings.h"
#include "input-controls.h"
#include "interp.h"
#include "physics.h"
#include "shader.h"
#include "loading-screen.h"
#include "lut.h"
#include "model.h"
#include "animator/character-anims.h"
#include "nav/roamer.h"
#include "noise.h"
#include "scene.h"
#include "sound.h"
#include "json.h"
#include "screen-record.h"
#include "ui-debug.h"
#include "ui-debug-fs.h"
#include "ui.h"
#include "worker.h"
#include <math.h>

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

struct character *scene_control_character(struct scene *s)
{
    if (!s->control)
        return NULL;

    cresp(character) cres = entity3d_character(s->control);
    return IS_CERR(cres) ? NULL : cres.val;
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
    clap_reserve_shadow_passes(s->clap_ctx, &s->camera->view, CASCADES_MAX);
    /* 0.9 lands the first divider at 15 units for a 500-unit far plane */
    s->camera->view.cascade_lambda   = 0.9;
    s->camera->view.shadow_distance  = 0.0;
    s->camera->view.proj_update      = true;
    s->camera->dist = 10;
    darray_init(s->camera->shakes);
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
        .input_field        = "File name:",
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

/**
 * hdr_transfer_name() - the debug panel's name for an HDR transfer
 * @transfer:   one of HDR_TRANSFER_*
 *
 * Return: a short static label, "?" for a value outside the enumeration
 */
static const char *hdr_transfer_name(int transfer)
{
    switch (transfer) {
        case HDR_TRANSFER_PQ:                   return "PQ";
        case HDR_TRANSFER_EXT_SRGB_LINEAR:      return "linear sRGB";
        case HDR_TRANSFER_EXT_P3_LINEAR:        return "linear P3";
        case HDR_TRANSFER_EXT_SRGB_NONLINEAR:   return "sRGB";
        case HDR_TRANSFER_EXT_P3_NONLINEAR:     return "P3";
        case HDR_TRANSFER_SCRGB:                return "scRGB";
        default:                                break;
    }

    return "?";
}

/* the AO radio labels, indexed by AO_METHOD_* */
static const char *ao_method_names[AO_METHOD_MAX] = {
    [AO_METHOD_SSAO]    = "SSAO",
    [AO_METHOD_GTAO]    = "GTAO",
};

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
        if (ropts->shadow_vsm)
            igSliderFloat("light bleed reduction", &ropts->shadow_bleed, 0.0, 0.99,
                          "%.2f", ImGuiSliderFlags_ClampOnInput);
        igSliderFloat("shadow caster min size (texels)", &ropts->shadow_caster_min_size,
                      0.0, 128.0, "%.0f", ImGuiSliderFlags_ClampOnInput);
        igSliderFloat("cascade lambda", &cam->view.cascade_lambda, 0.0, 1.0, "%.2f",
                      ImGuiSliderFlags_ClampOnInput);
        igSliderFloat("shadow distance", &cam->view.shadow_distance, 0.0,
                      cam->view.main.far_plane, "%.0f", ImGuiSliderFlags_ClampOnInput);
        igText("cascades: %.1f | %.1f | %.1f | %.1f", cam->view.divider[0],
               cam->view.divider[1], cam->view.divider[2], cam->view.divider[3]);
        igCheckbox("shadow msaa", &ropts->shadow_msaa);
        igCheckbox("model msaa", &ropts->model_msaa);
        igSliderFloat("render scale", &ropts->render_scale, 0.25, 1.0, "%.2f",
                      ImGuiSliderFlags_ClampOnInput);
        igCheckbox("edges", &ropts->edges);
        igBeginDisabled(!ropts->edges);
        igCheckbox("edge sobel", &ropts->edge_sobel);
        /* SMAA has no effect while TAA is on */
        igBeginDisabled(ropts->taa);
        igCheckbox("edge antialiasing", &ropts->edge_antialiasing);
        igEndDisabled();
        igEndDisabled();
        if (IS_DEFINED(CONFIG_TAA)) {
            igCheckbox("TAA", &ropts->taa);
            if (ropts->taa)
                igSliderFloat("TAA feedback", &ropts->taa_feedback, 0.02, 1.0, "%.2f",
                              ImGuiSliderFlags_ClampOnInput);
        }
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
        igCheckbox("light draws", &ropts->light_draws_enabled);
        igCheckbox("light heatmap", &ropts->light_heatmap);
        ui_igHelpTooltip("Overlay the light grid's tile occupancy on the scene, blue (no lights) "
                         "through green to red (five and up); rebuilds the pipeline");
        igCheckbox("aabb draws", &ropts->aabb_draws_enabled);
        igCheckbox("degenerate collision draws", &ropts->degenerate_draws_enabled);
        ui_igHelpTooltip("Paint the collision triangles that the physics backend dropped "
                         "as degenerate, on the models flagged in the entity inspector");
        igText("AO:");
        igSameLine(0.0, 0.0);
        igPushID_Str("ao");
        if (igRadioButton_Bool("off", !ropts->ssao))    ropts->ssao = false;
        for (int i = 0; i < AO_METHOD_MAX; i++) {
            igSameLine(0.0, 0.0);
            if (igRadioButton_Bool(ao_method_names[i], ropts->ssao && ropts->ao_method == i)) {
                ropts->ssao = true;
                ropts->ao_method = i;
            }
        }
        igPopID();
        if (ropts->ssao) {
            igSliderFloat("AO radius", &ropts->ssao_radius, 0.1, 2.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
            igSliderFloat("AO weight", &ropts->ssao_weight, 0.0, 1.0, "%.4f", ImGuiSliderFlags_ClampOnInput);
        }
        igCheckbox("volumetric fog", &ropts->fog_volumetric);
        if (ropts->fog_volumetric) {
            igCheckbox("-> debug view", &ropts->fog_volumetric_debug);
            igSliderFloat("-> intensity", &ropts->volumetric_intensity, 0.0, 50.0, "%.1f", ImGuiSliderFlags_ClampOnInput);
            igSliderFloat("-> anisotropy", &ropts->volumetric_g, -0.99, 0.99, "%.2f", ImGuiSliderFlags_ClampOnInput);
            igSliderFloat("-> density", &ropts->volumetric_density, 0.0, 1.0, "%.3f", ImGuiSliderFlags_ClampOnInput);
            igSliderFloat("-> noise amp", &ropts->volumetric_amp, 0.0, 2.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
            igSliderFloat("-> noise scale", &ropts->volumetric_scale, 0.001, 0.5, "%.3f", ImGuiSliderFlags_ClampOnInput);
            igSliderFloat("-> local intensity", &ropts->volumetric_local_intensity, 0.0, 100.0, "%.1f", ImGuiSliderFlags_ClampOnInput);
            igSliderFloat("-> wind", &ropts->volumetric_wind, 0.0, 5.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
            igSliderFloat("-> falloff", &ropts->volumetric_falloff, 1.0, 32.0, "%.1f", ImGuiSliderFlags_ClampOnInput);
        }
        igCheckbox("use HDR pipeline", &ropts->hdr);
        if (igCheckbox("enable HDR output", &ropts->hdr_output_enabled))
            renderer_hdr_enable(clap_get_renderer(scene->clap_ctx), ropts->hdr_output_enabled);
        if (ropts->hdr_output) {
            int transfer = ropts->hdr_transfer_want;
            unsigned int transfers = renderer_hdr_transfers(clap_get_renderer(scene->clap_ctx));

            /* one radio per encoding the surface can actually present */
            igText("HDR transfer:");
            igPushID_Str("hdrtf");
            for (int i = 0; i < HDR_TRANSFER_MAX; i++) {
                if (!(transfers & (1u << i)))   continue;
                igSameLine(0.0, 0.0);
                igRadioButton_IntPtr(hdr_transfer_name(i), &transfer, i);
            }
            igPopID();

            if (transfer != ropts->hdr_transfer_want) {
                ropts->hdr_transfer_want = transfer;
                renderer_hdr_transfer_set(clap_get_renderer(scene->clap_ctx), transfer);
            }
            if (ropts->hdr_transfer != ropts->hdr_transfer_want)
                igText("surface gave %s instead", hdr_transfer_name(ropts->hdr_transfer));
        }

        igSliderFloat("bloom exposure", &ropts->bloom_exposure, 0.01, 5.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
        igSliderFloat("bloom intensity", &ropts->bloom_intensity, 0.1, 10.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
        igSliderFloat("bloom threshold", &ropts->bloom_threshold, 0.01, 1.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
        igSliderFloat("bloom distance", &ropts->bloom_distance, 0.0, 100.0, "%.1f", ImGuiSliderFlags_ClampOnInput);
        int bq = ropts->bloom_quality;
        igText("bloom quality:");
        igSameLine(0.0, 0.0);
        igPushID_Str("bq");
        for (int i = 0; i < BLOOM_QUALITY_MAX; i++) {
            igSameLine(0.0, 0.0);
            igRadioButton_IntPtr(graphics_settings_bloom_quality_name(i), &bq, i);
        }
        igPopID();
        if (bq != ropts->bloom_quality) graphics_settings_set_bloom_quality(scene->clap_ctx, bq);
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
        if (ropts->hdr_output) {
            igSliderFloat("peak nits", &ropts->hdr_peak_nits, 1.0, 20000.0, "%.1f", 0);
            igSliderFloat("white nits", &ropts->hdr_white_nits, 1.0, ropts->hdr_peak_nits, "%.1f", 0);
            igSliderFloat("compress knee", &ropts->hdr_compress_knee, 0.1, 10.0, "%.1f", 0);
            igSliderFloat("knee softness", &ropts->hdr_knee_softness, 1.0, 64.0, "%.1f", 0);
        }
        igSliderFloat("contrast", &ropts->contrast, 0.01, 1.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
        igSeparator();
        igCheckbox("fog", &ropts->fog);
        if (ropts->fog) {
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
            igSliderFloat("fog ceiling", &ropts->fog_max, 0.0, 1.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
            igText("fog curve:");
            igSameLine(0.0, 0.0);
            igPushID_Str("fog_curve");
            igRadioButton_IntPtr("linear", &ropts->fog_curve, FOG_CURVE_LINEAR);
            igSameLine(0.0, 0.0);
            igRadioButton_IntPtr("exp", &ropts->fog_curve, FOG_CURVE_EXP);
            igSameLine(0.0, 0.0);
            igRadioButton_IntPtr("smooth", &ropts->fog_curve, FOG_CURVE_SMOOTH);
            igPopID();
        }
        igSeparator();
        igCheckbox("film grain", &ropts->film_grain);
        igSliderFloat("grain factor", &ropts->film_grain_factor, 0.0, 1.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
        igSliderFloat("grain power", &ropts->film_grain_power, 0.0, 2.0, "%.2f", ImGuiSliderFlags_ClampOnInput);
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
            if (!light_is_valid(&scene->light, idx))
                continue;
            igPushID_Int(idx);
            ui_igControlTableHeader("light %d", "pos", idx);

            ui_igCheckbox("directional", (bool *)&scene->light.is_dir[idx]);
            ui_igCheckbox("draw direction", &scene->light.draw_direction[idx]);
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
            ui_igSliderFloat("fall drag", &c->fall_drag, 0.0, 20.0, "%f", ImGuiSliderFlags_AlwaysClamp);
            ui_igSliderFloat("speed", &c->speed, 0.1, 10.0, "%f", ImGuiSliderFlags_AlwaysClamp);
            ui_igCheckbox("can jump", &c->can_jump);
            ui_igCheckbox("can dash", &c->can_dash);
            ui_igCheckbox("can levitate", &c->can_levitate);
            ui_igCheckbox("turns with ground", &c->turns_with_ground);
            ui_igCheckbox("tilts with ground", &c->tilts_with_ground);
            ui_igCheckbox("bobs head", &c->bobs_head);
            igPopID();
            igEndTable();
        }
    }

    ui_igEnd(DEBUG_CHARACTERS);
}

/*
 * Flag a model whose collision mesh has holes where the physics backend
 * dropped degenerate triangles. "degenerate collision draws" paints them
 * in the scene.
 */
static void model_degenerate_warn(model3d *m)
{
    if (!model3d_has_degenerate_tris(m))    return;

    ui_igWarnTooltip("%zu degenerate collision triangle(s): the collision mesh has holes "
                     "where the renderer shows a surface", m->nr_degenerate_tri);
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

            /* XXX: this has problems: e.g., when a new scene is loaded */
            if (ei->entity && ei->entity->txmodel == txm)
                selected = true;

            igPushID_Ptr(txm);

            bool picked = igSelectable_Bool(txmodel_name(txm), selected,
                                            selected ? ImGuiSelectableFlags_Highlight : 0, (ImVec2){0, 0});
            /* default focus applies to the last item, so it goes before the mark */
            if (picked)     igSetItemDefaultFocus();
            model_degenerate_warn(txm->model);

            if (picked) {
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

    if (ei->entity)
        model_degenerate_warn(ei->entity->txmodel->model);

    ui_igCheckbox("switch scene control", &ei->switch_control);
    ui_igHelpTooltip(
            "Switch scene control to the selected model / entity. "
            "You can move the entity with motion controls only"
            "when it's the controlled entity"
    );

    ui_igCheckbox("follow scene control", &ei->follow_control);
    ui_igHelpTooltip("Automatically switched to the control entity");

    if (ei->entity) {
        model3dtx *txm = ei->entity->txmodel;
        render_options *ropts = clap_get_render_options(scene->clap_ctx);
        shader_variants_t variants = model3dtx_shader_variants(txm) |
                                     ropts_shader_variants(ropts);
        char vbuf[144];     /* shader_variants_str() truncates past this */

        ui_igLabel("shader");
        igTableNextColumn();
        igText("%s", shader_name(txm->model->prog));

        ui_igLabel("variants");
        igTableNextColumn();
        igTextWrapped("%s", shader_variants_str(variants, vbuf, sizeof(vbuf)));
    }

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

        ui_igLabel("normals");
        igTableNextColumn();
        igText("noise:");
        igSameLine(0.0, 0.0);
        igPushID_Str("nono");
        igRadioButton_IntPtr("off", &mat->use_noise_normals, NOISE_NORMALS_NONE);
        igSameLine(0.0, 0.0);
        igRadioButton_IntPtr("GPU", &mat->use_noise_normals, NOISE_NORMALS_GPU);
        igSameLine(0.0, 0.0);
        igRadioButton_IntPtr("3D", &mat->use_noise_normals, NOISE_NORMALS_3D);

        if (mat->use_noise_normals) {
            ui_igSliderFloat("-> scale", &mat->noise_normals_scale, 0.01, 5.0, "%.04f", 0);
            ui_igSliderFloat("-> amp", &mat->noise_normals_amp, 0.1, 1.0, "%.04f", 0);
        }
        igPopID();

        ui_igCheckbox("use 3D fog", &mat->use_3d_fog);
        if (mat->use_3d_fog) {
            igPushID_Str("fog");
            ui_igSliderFloat("-> scale", &mat->fog_3d_scale, 0.0, 1.0, "%.06f", 0);
            ui_igSliderFloat("-> amp", &mat->fog_3d_amp, 0.0, 2.0, "%.06f", 0);
            igPopID();
        }

        ui_igCheckbox("noise emission", &mat->use_noise_emission);
        if (mat->use_noise_emission)
            ui_igColorEdit3("color", mat->noise_emission_color,
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_NoLabel  |
                ImGuiColorEditFlags_NoTooltip
            );

        if (mat->use_noise_normals || mat->use_noise_emission)
            ui_igSliderFloat3("noise drift", mat->noise_drift, -2.0, 2.0, "%.03f", 0);

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
        /*
         * Repeats per UV unit: per meter for meter UVs, where 0.01 is one
         * repeat per 100 m. Logarithmic, so the range below 1 gets as much
         * of the slider as each decade above it.
         */
        ui_igSliderFloat("UV factor", &mat->uv_factor, 0.01f, 200.0f, "%f", ImGuiSliderFlags_Logarithmic);
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

        bool visible = entity3d_matches(e, ENTITY3D_VISIBLE);
        if (ui_igCheckbox("visible", &visible))
            entity3d_visible(e, visible);

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

        if (light_is_valid(&scene->light, e->light_idx)) {
            ui_igLabel("light idx");
            igTableNextColumn();
            igText("%d", e->light_idx);
        }
        igEndTable();

        /*
         * Attachment group: shows parent linkage and the entity's
         * xform interpreted as an offset from the parent (joint or
         * world). These read from the same e->xform the "entity"
         * table does, but are editable here in attachment-local
         * terms so it's obvious which numbers came from the scene
         * file's "attach"/"rotate"/"position" entries.
         */
        if (e->parent && ui_igControlTableHeader("attachment", "parent joint")) {
            ui_igLabel("parent");
            igTableNextColumn();
            igText("%s", entity_name(e->parent));

            ui_igLabel("parent joint");
            igTableNextColumn();
            if (e->parent_joint == JOINT_TYPE_MAX) {
                igText("<world>");
            } else {
                model3d *pm = e->parent->txmodel->model;
                igText("%d (%s)", e->parent_joint,
                       (unsigned)e->parent_joint < pm->nr_joints
                           ? pm->joints[e->parent_joint].name
                           : "<invalid>");
            }

            vec3 local_pos;
            transform_pos(&e->xform, local_pos);
            ui_igLabel("local pos");
            igTableNextColumn();
            if (igDragFloat3("##att_pos", local_pos, 0.01, -500.0, 500.0,
                             "%.03f", 0))
                entity3d_position(e, local_pos);

            int att_rotated = 0;
            float att_angles[3];
            transform_rotation(&e->xform, att_angles, true);
            if (ui_igSliderFloat("local rx", &att_angles[0], -180.0, 180.0,
                                 "%.02f", 0))
                att_rotated++;
            if (ui_igSliderFloat("local ry", &att_angles[1], -180.0, 180.0,
                                 "%.02f", 0))
                att_rotated++;
            if (ui_igSliderFloat("local rz", &att_angles[2], -180.0, 180.0,
                                 "%.02f", 0))
                att_rotated++;
            if (att_rotated)
                transform_set_angles(&e->xform, att_angles, true);

            ui_igSliderFloat("scale", &e->scale, 0.01, 10.0, "%.03f", 0);

            igEndTable();
        }

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

static void scene_navmesh_debug(struct scene *scene)
{
    debug_module *dbgm = ui_igBegin(DEBUG_NAVMESH, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display) return;

    if (dbgm->unfolded && scene->nm) {
        cerr err = navmesh_debug_draw(scene->nm, scene);

        if (!IS_CERR(err)) {
            bool shown = navmesh_debug_shown(scene->nm);

            if (igCheckbox("draw walkable surface", &shown))    navmesh_debug_show(scene->nm, shown);
        } else {
            char buf[128];

            cerr_strbuf(buf, sizeof(buf), &err);
            igText("overlay failed: %s", buf);
        }
    }

    ui_igEnd(DEBUG_NAVMESH);
}

static void scene_roamers_debug(struct scene *scene)
{
    if (scene->nm)  navmesh_roamers_debug(scene->nm, scene);
}

#else
static inline void scene_parameters_debug(struct scene *scene, int cam_idx) {}
static inline void light_debug(struct scene *scene) {}
static inline void scene_characters_debug(struct scene *scene) {}
static inline void scene_entity_inspector_debug(struct scene *scene) {}
static inline void scene_debug_frusta(struct scene *scene, struct view *view) {}
static inline void scene_navmesh_debug(struct scene *scene) {}
static inline void scene_roamers_debug(struct scene *scene) {}
#endif /* CONFIG_FINAL */

static void scene_camera_calc(struct scene *s, int camera)
{
    int width, height;
    clap_get_viewport(s->clap_ctx, NULL, NULL, &width, &height);

    struct camera *cam = &s->cameras[camera];
    view_update_perspective_projection(s->clap_ctx, &cam->view, width, height,
                                       cam->zoom ? 0.5 : 1.0);

    camera_update(s->camera, s);

    camera_view_update(cam, s->clap_ctx);
    view_calc_frustum(s->clap_ctx, &cam->view);

    // entity3d *env = cam->bv;
    cam->bv = NULL;

    float near_backup = 0.0;
    near_backup = cbrtf(
        (s->bv[1][0] - s->bv[0][0]) *
        (s->bv[1][1] - s->bv[0][1]) *
        (s->bv[1][2] - s->bv[0][2])
    );
    // if (env) {
    //     vec3 light_dir;
    //     /*
    //      * Calculate frusta's near plane extension from the bounding volume's
    //      * dimensions and the light direction vector.
    //      * TODO: this does not take into account the camera's position within
    //      * the AABB.
    //      */
    //     vec3_norm_safe(light_dir, &s->light.dir[0]);
    //     float xz_mix = linf_interp(
    //         entity3d_aabb_Z(env),
    //         entity3d_aabb_X(env),
    //         fabsf(vec3_mul_inner(light_dir, (vec3){ 1.0, 0.0, 0.0 }))
    //     );
    //     float ycos = fmaxf(fabsf(vec3_mul_inner(light_dir, (vec3){ 0.0, 1.0, 0.0 })), 0.2);
    //     near_backup = min(xz_mix / ycos, entity3d_aabb_avg_edge(env));
    // }
    /*
     * Only the first light source gets to cast shadows for now;
     * spotlights get their view from light_update_*() callbacks.
     */
    if (!light_is_spotlight(&s->light, 0)) {
        bool shadow_vsm = clap_get_render_options(s->clap_ctx)->shadow_vsm;
        view_update_from_frustum(s->clap_ctx, &s->light.view[0], &cam->view, &s->light.dir[0 * 3], near_backup, !shadow_vsm);
        view_calc_frustum(s->clap_ctx, &s->light.view[0]);
    }
}

void scene_cameras_calc(struct scene *s)
{
    int i;

    for (i = 0; i < s->nr_cameras; i++)
        scene_camera_calc(s, i);

    /*
     * The grid bins the lights into the screen tiles of the view this
     * frame renders with, so it follows the camera's: binned against
     * the previous frame's view, the tiles along a lit area's edge drop
     * its lights whenever the camera moves
     */
    light_grid_compute(&s->light, &s->camera->view);
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

/*
 * MOUSE_BASELINE is the camera rotation in degrees that one mouse pixel
 * produces at 1.0x sensitivity; YAW_SPEED_SCALE is the extra horizontal
 * rotation speed factor on top of scene::ang_speed. Both are candidates
 * for becoming scene or game parameters.
 */
constexpr float MOUSE_BASELINE  = 0.05f;
constexpr float YAW_SPEED_SCALE = 1.5f;

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
    if ((m->input.tab || m->input.stick_r) && !scene_is_loading(s)) scene_control_next(s);
#endif
    if (m->input.resize) {
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
    if (clap_is_paused(s->clap_ctx) || scene_is_loading(s)) return 0;

    /*
     * Input-source filter: drop motion/action messages from gamepads that
     * aren't the active one (so a forgotten plugged-in drifting controller
     * doesn't steer the character) and clamp or drop mouse deltas on the
     * keyboard source based on use_mouse_for_camera / sensitivity / invert_y.
     */
    struct message filtered = *m;
    if (m->source && m->source->type == MST_JOYSTICK) {
        const char *active = input_controls_active_gamepad_name();
        if (!active || !m->source->name || strcmp(m->source->name, active))  return 0;
    } else if (m->source && m->source->type == MST_KEYBOARD) {
        /*
         * Mouse deltas arrive in pixels, while scene_update() feeds rs_d*
         * through ang_speed assuming stick-sized (~[-1, 1]) values, and
         * camera_move() divides the resulting rate by fps. Multiplying by
         * fps here cancels that division, so that one pixel at sens=1.0
         * yields MOUSE_BASELINE degrees of rotation independently of the
         * frame rate. The yaw path multiplies by an extra YAW_SPEED_SCALE,
         * so divide that out here.
         */
        if (input_controls_use_mouse() && s->ang_speed > 0.0f) {
            unsigned long fps = clap_get_fps_fine(s->clap_ctx);
            float sens  = input_controls_mouse_sensitivity();
            float yflip = input_controls_invert_y() ? -1.0f : 1.0f;
            float pitch_factor = sens * yflip * MOUSE_BASELINE * (float)fps / s->ang_speed;
            float yaw_factor   = sens         * MOUSE_BASELINE * (float)fps / (s->ang_speed * YAW_SPEED_SCALE);
            filtered.input.delta_ry *= pitch_factor;
            filtered.input.delta_rx *= yaw_factor;
        } else {
            filtered.input.delta_rx = filtered.input.delta_ry = 0;
        }
    }

    if (filtered.input.zoom == 1) {
        if (!s->camera->zoom)
            s->camera->view.proj_update = true;
        s->camera->zoom = 1;
    } else if (filtered.input.zoom == 2) {
        if (s->camera->zoom)
            s->camera->view.proj_update = true;
        s->camera->zoom = 0;
    }

    motion_parse_input(&s->mctl, &filtered);

    struct character *current = scene_control_character(s);
    if (current)
        character_handle_input(current, s, &filtered);

    return 0;
}

int scene_add_model(struct scene *s, model3dtx *txm)
{
    mq_add_model(&s->mq, txm);
    return 0;
}

/* Feed one static collision trimesh to the navmesh; hazards obstruct. */
static void navmesh_add_entity(entity3d *e, void *data)
{
    navmesh *nm = data;
    model3d *m = e->txmodel->model;

    /* only static collision geoms feed the navmesh; skip dynamic bodies */
    if (!entity3d_is(e, &E3DQ(.all_of = ENTITY3D_HAS_PHYSICS,
                              .none_of = ENTITY3D_PHYS_IS_BODY | ENTITY3D_NM_SKIP)))
        return;
    if (!m->collision_vx || !m->collision_idx)  return;

    CERR_RET(navmesh_add(nm,
                         .vx       = m->collision_vx,
                         .vxsz     = m->collision_vxsz,
                         .idx      = m->collision_idx,
                         .idxsz    = m->collision_idxsz,
                         .pos      = transform_pos(&e->xform, nullptr),
                         .rot      = transform_rotation_quat(&e->xform),
                         .scale    = e->parent ? e->parent->scale : e->scale,
                         .obstacle = entity3d_matches(e, ENTITY3D_NM_HAZARD)),
             err_cerr(__cerr, "navmesh: skipping '%s'\n", m->name));
}

cerr scene_navmesh_init(struct scene *scene)
{
    navmesh *nm = CRES_RET_CERR(_navmesh_new(&scene->nm_params));

    mq_for_each(&scene->mq, navmesh_add_entity, nm);

    CERR_RET(navmesh_bake_largest(nm), { navmesh_free(nm); return __cerr; });

    if (scene->nm)  navmesh_free(scene->nm);
    scene->nm = nm;

    trace("navmesh: %zu walkable triangles\n", navmesh_nr_tris(scene->nm));

    return CERR_OK;
}

navmesh *scene_navmesh(struct scene *scene)
{
    return scene->nm;
}

static void scene_load_step(struct scene *scene);

void scene_update(struct scene *scene)
{
    scene_parameters_debug(scene, 0);
    scene_characters_debug(scene);
    scene_entity_inspector_debug(scene);
    scene_navmesh_debug(scene);
    scene_roamers_debug(scene);
    light_debug(scene);
    screen_record_update(scene);
    scene_load_step(scene);

    /*
     * Fire per-light update callbacks before mq_update() so they observe
     * transform_is_updated() before default_update() clears it.
     */
    light_update(&scene->light);
    mq_update(&scene->mq);

    if (scene->mctl.rs_dy) {
        float delta = scene->mctl.rs_dy * scene->ang_speed;

        camera_add_pitch(scene->camera, delta);
        transform_set_updated(&scene->camera->xform);
    }
    if (scene->mctl.rs_dx) {
        camera_add_yaw(scene->camera, scene->mctl.rs_dx * scene->ang_speed * YAW_SPEED_SCALE);
        transform_set_updated(&scene->camera->xform);
    }

    clap_context *ctx = scene->clap_ctx;
    camera_move(scene->camera, clap_get_fps_fine(ctx));

    if (clap_get_render_options(ctx)->camera_frusta_draws_enabled)
        scene_debug_frusta(scene, &scene->camera->view);
    if (clap_get_render_options(ctx)->light_frusta_draws_enabled)
        scene_debug_frusta(scene, &scene->light.view[0]);
    if (clap_get_render_options(ctx)->light_draws_enabled)
        light_draw(ctx, &scene->light);
    light_draw_directions(ctx, &scene->light);

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

    light_init(ctx, &scene->light);

    light_set_ambient(&scene->light, (float[]){ 0.1, 0.1, 0.1 });
    light_set_shadow_tint(&scene->light, (float[]){ 0.1, 0.1, 0.1 });

    /* messagebus_done() will "unsubscribe" (free) these */
    CERR_RET_CERR(subscribe(ctx, MT_INPUT, scene_handle_input, scene));

    vec3_dup(scene->bv[0], (vec3){ INFINITY, INFINITY, INFINITY });
    vec3_dup(scene->bv[1], (vec3){ -INFINITY, -INFINITY, -INFINITY });

    scene->initialized = true;

    return CERR_OK;
}

static sfx *scene_get_sfx(struct scene *s, entity3d *e, const char *name)
{
    cresp(character) cres = entity3d_character(e);
    sfx *sfx = NULL;

    if (!IS_CERR(cres) && !cres.val->airborne) {
        struct character *c = cres.val;
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

/*
 * Add physics to an entity from parameters parsed out of the scene JSON
 * "physics" block or a GLTF node's clap::physics::* extras.
 */
static void scene_entity_add_physics(struct scene *scene, entity3d *e, double mass,
                                     geom_class class, phys_type ptype, double geom_off,
                                     double geom_radius, double geom_length,
                                     double bounce, double bounce_vel)
{
    entity3d_add_physics(e, clap_get_phys(scene->clap_ctx), mass, class,
                         ptype, geom_off, geom_radius, geom_length);
    if (!e->phys_body)  return;

    phys_body_set_contact_params(e->phys_body,
                                 .bounce = bounce,
                                 .bounce_vel = bounce_vel);
}

/* @prim is skinned with @skin */
static bool prim_skinned_with(struct gltf_data *gd, int prim, int skin)
{
    cres(int) s = gltf_mesh_skin(gd, prim);

    return !IS_CERR(s) && s.val == skin;
}

/*
 * For multi-primitive meshes, spawn a child entity per sibling primitive,
 * parented to @e. Children have identity local TRS, so they inherit the
 * parent's world transform. A primitive whose material is a fallback (no
 * texture, no baseColorFactor) loads but isn't rendered — scene logic may
 * still reference it.
 *
 * When @add_phys, visible children also get their own static trimesh
 * collider, so the whole mesh contributes collision, not just the parent
 * primitive.
 *
 * A child skinned with the parent's skin (another primitive of the same
 * skinned mesh, or a part of a multi-mesh rig) draws with the parent's
 * pose: the parent's animator moves the whole thing.
 */
static void scene_spawn_prim_children(struct scene *scene, entity3d *e, struct gltf_data *gd,
                                      model3dtx **prim_txms, const int *prims, int prim_count,
                                      bool add_phys)
{
    cres(int) skin = gltf_mesh_skin(gd, prims[0]);

    for (int sp = 1; sp < prim_count; sp++) {
        entity3d *child = ref_new(entity3d, .txmodel = prim_txms[sp]);

        child->parent = e;
        if (gltf_mesh_fallback(gd, prims[sp])) {
            entity3d_clear(child, ENTITY3D_VISIBLE);
            continue;
        }

        if (!IS_CERR(skin) && prim_skinned_with(gd, prims[sp], skin.val) && e->joints) {
            entity3d_set(child, ENTITY3D_PARENT_POSE, nullptr);
            /* the pose carries it outside its bind pose bounds, as it does the parent */
            if (entity3d_matches(e, ENTITY3D_SKIP_CULLING)) entity3d_set(child, ENTITY3D_SKIP_CULLING, nullptr);
        }

        if (!add_phys)          continue;

        /* mass/radius/length are not used for static trimeshes */
        entity3d_add_physics(child, clap_get_phys(scene->clap_ctx), 1.0,
                             GEOM_TRIMESH, PHYS_GEOM, 0.0, 1.0, 1.0);
        if (!child->phys_body)  continue;

        /*
         * The child's local TRS stays identity for rendering (see
         * parent_transform_apply()), and its geom only follows the parent
         * from the child's first update on, so place the static geom at
         * the parent's world position/rotation explicitly.
         */
        phys_body_set_position(child->phys_body, transform_pos(&e->xform, NULL));
        phys_body_rotate_xform(child->phys_body, &e->xform);
    }
}

/* A model whose root node is a bare armature (no mesh of its own) is a rig: the skin of its primary primitive */
static cres(int) scene_rig_skin(struct gltf_data *gd, int prim_first)
{
    return gltf_root_mesh(gd) < 0 ? gltf_mesh_skin(gd, prim_first) : cres_error(int, CERR_NOT_FOUND);
}

/*
 * The primitives a model entry instantiates, the parent's first: the primary
 * group's, and, for a rig, every other group skinned with the same skin. A
 * rig built of several meshes, one node each, is one model: those are its
 * parts. @prims has room for all of @gd's primitives.
 */
static cres(int) scene_model_prims(struct gltf_data *gd, int *prims)
{
    int primary_group = CRES_RET_SCALAR(gltf_primary_group(gd), int);
    int prim_count = CRES_RET_SCALAR(gltf_group_count(gd, primary_group), int);
    int prim_first = CRES_RET_SCALAR(gltf_group_first(gd, primary_group), int);
    cres(int) rig_skin = scene_rig_skin(gd, prim_first);
    bool rig = !IS_CERR(rig_skin);

    for (int p = 0; p < prim_count; p++)    prims[p] = prim_first + p;

    for (int g = 0; rig && g < gltf_get_mesh_groups(gd); g++) {
        if (g == primary_group)     continue;

        int first = CRES_RET(gltf_group_first(gd, g), continue);
        if (!prim_skinned_with(gd, first, rig_skin.val))    continue;

        int count = CRES_RET_SCALAR(gltf_group_count(gd, g), int);
        for (int p = 0; p < count; p++)     prims[prim_count++] = first + p;
    }

    return cres_val(int, prim_count);
}

/* A model's "physics" block, as the bodies of its entities get it */
struct scene_physics {
    double      mass;
    double      bounce;
    double      bounce_vel;
    double      geom_off;
    double      geom_radius;
    double      geom_length;
    geom_class  class;
    phys_type   type;
};

/* Parse @phys into @sp, or take the defaults without it */
static void scene_physics_parse(struct scene_physics *sp, JsonNode *phys)
{
    *sp = (struct scene_physics){
        .mass           = 1.0,
        .bounce         = 0.0,
        .bounce_vel     = DINFINITY,
        .geom_off       = 0.0,
        .geom_radius    = 1.0,
        .geom_length    = 0.0,
        .class          = GEOM_TRIMESH,
        .type           = PHYS_GEOM,
    };

    if (!phys)  return;

    for (JsonNode *p = phys->children.head; p; p = p->next) {
        if (p->tag == JSON_NUMBER && !strcmp(p->key, "bounce"))             sp->bounce = p->number_;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "bounce_vel"))    sp->bounce_vel = p->number_;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "mass"))          sp->mass = p->number_;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "yoffset"))       sp->geom_off = p->number_;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "radius"))        sp->geom_radius = p->number_;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "length"))        sp->geom_length = p->number_;
        else if (p->tag == JSON_STRING && !strcmp(p->key, "geom")) {
            if (!strcmp(p->string_, "trimesh"))         sp->class = GEOM_TRIMESH;
            else if (!strcmp(p->string_, "sphere"))     sp->class = GEOM_SPHERE;
            else if (!strcmp(p->string_, "capsule"))    sp->class = GEOM_CAPSULE;
        } else if (p->tag == JSON_STRING && !strcmp(p->key, "type")) {
            if (!strcmp(p->string_, "body"))        sp->type = PHYS_BODY;
            else if (!strcmp(p->string_, "geom"))   sp->type = PHYS_GEOM;
        }
    }
}

/*
 * A model entry's glTF on its way in: read on the main thread (the
 * librarian's handles are reference counted), then parsed and its
 * primitives prepared by model_prep_build(), on the loader thread or in
 * the add step itself
 */
typedef struct model_prep {
    /* the main thread's reference to the loader job, until its done() */
    job                 *job;
    struct gltf_data    *gd;
    gltf_load_options   load;
    cerr                err;
    /* the entities get static trimesh bodies: cook their collider too */
    bool                cook_trimesh;
    /* done() has run: @gd and @err are the main thread's */
    bool                delivered;
    /* the load is over without it: done() frees it */
    bool                abandoned;
} model_prep;

cresp_ret(model_prep);

static void model_prep_free(model_prep *mp)
{
    if (mp->gd) gltf_free(mp->gd);
    mem_free(mp->load.buf);
    mem_free((void *)mp->load.name);
    mem_free(mp);
}

static DEFINE_CLEANUP(model_prep, if (*p) model_prep_free(*p))

/* Read a model entry's glTF, for model_prep_build() to parse */
static cresp(model_prep) model_prep_new(struct scene *scene, JsonNode *node)
{
    struct clap_config *clap_cfg = clap_get_config(scene->clap_ctx);
    const char *gltf = nullptr;
    JsonNode *phys = nullptr;
    bool fix_origin = false;

    if (node->tag != JSON_OBJECT)   return cresp_error(model_prep, CERR_PARSE_FAILED);

    for (JsonNode *p = node->children.head; p; p = p->next) {
        if (p->tag == JSON_STRING && !strcmp(p->key, "gltf"))           gltf = p->string_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "fix_origin"))  fix_origin = p->bool_;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "physics"))   phys = p;
    }

    if (!gltf)  return cresp_error(model_prep, CERR_PARSE_FAILED);

    LOCAL_SET(model_prep, mp) = mem_alloc(sizeof(*mp), .zero = 1);
    if (!mp)    return cresp_error(model_prep, CERR_NOMEM);

    /* the scene file may go before the preparation does */
    mp->load.name = strdup(gltf);
    if (!mp->load.name) return cresp_error(model_prep, CERR_NOMEM);

    mp->load.fix_origin = fix_origin;
    mp->load.parts = !!json_find_member(node, "limbs");
    mp->load.model_make = clap_cfg->model_make;
    mp->load.model_make_priv = clap_cfg->model_make_priv;
    mp->err = gltf_read(&mp->load);

    if (phys) {
        struct scene_physics sp;

        scene_physics_parse(&sp, phys);
        mp->cook_trimesh = sp.class == GEOM_TRIMESH && sp.type == PHYS_GEOM;
    }

    return cresp_val(model_prep, NOCU(mp));
}

/*
 * Parse the glTF and prepare the primitives the model is made of: no file,
 * engine object or reference count, so the loader thread can; @j, if it's
 * a job, stops it between primitives once it's cancelled
 */
static cerr model_prep_build(model_prep *mp, job *j)
{
    LOCAL_SET(gltf_data, gd) = _gltf_load(&mp->load);
    mem_free(mp->load.buf);
    mp->load.buf = nullptr;
    if (!gd)    return CERR_PARSE_FAILED;

    LOCAL_SET(int, prims) = mem_alloc(sizeof(*prims), .nr = gltf_get_meshes(gd));
    if (!prims) return CERR_NOMEM;

    int count = CRES_RET_CERR(scene_model_prims(gd, prims));
    for (int p = 0; p < count; p++) {
        if (j && job_is_cancelled(j))   return CERR_CANCELLED;
        CERR_RET_CERR(gltf_prepare(gd, prims[p]));

        /*
         * Cook the collider of each primitive that gets a static trimesh
         * body: the first, and every sibling that isn't a fallback. Without
         * a job, the bodies come right after and cook their own.
         */
        if (!j || !mp->cook_trimesh)                    continue;
        if (p > 0 && gltf_mesh_fallback(gd, prims[p]))  continue;
        if (job_is_cancelled(j))                        return CERR_CANCELLED;
        gltf_prepare_collision(gd, prims[p]);
    }

    mp->gd = NOCU(gd);

    return CERR_OK;
}

static cerr model_prep_run(job *j)
{
    return model_prep_build(job_get_data(j), j);
}

static void model_prep_done(job *j, cerr err)
{
    model_prep *mp = job_get_data(j);

    /* the main thread's reference; the poll holds the pool's */
    ref_put(j);
    mp->job = nullptr;

    if (mp->abandoned) {
        model_prep_free(mp);
        return;
    }

    mp->err = err;
    mp->delivered = true;
}

/** SCENE_LOAD_AHEAD - models given to the loader thread and not in yet: the one up next and one more */
#define SCENE_LOAD_AHEAD 2

/* Read a model entry's glTF and have the loader thread prepare it; nullptr: the add step prepares it */
static model_prep *model_prep_submit(struct scene *scene, JsonNode *node)
{
    model_prep *mp = CRES_RET(model_prep_new(scene, node), return nullptr);

    /* a file that doesn't read is the model's error, in its turn */
    if (IS_CERR(mp->err)) {
        mp->delivered = true;
        return mp;
    }

    cresp(job) res = job_submit(.pool = scene->loader_pool, .run = model_prep_run, .done = model_prep_done,
                                .data = mp, .name = "scene model");
    if (IS_CERR(res)) {
        mp->err = cerr_error_cres(res);
        mp->delivered = true;
        return mp;
    }

    mp->job = res.val;

    return mp;
}

void scene_bv_update(struct scene *s, entity3d *e)
{
    for (int i = 0; i < 3; i++) {
        s->bv[0][i] = fminf(s->bv[0][i], e->aabb[0][i]);
        s->bv[1][i] = fmaxf(s->bv[1][i], e->aabb[1][i]);
    }
}

static void scene_loader_hold(struct scene_loader *sl, model3dtx *txm);

/*
 * @mp: the model's glTF, prepared on the loader thread; nullptr prepares it
 * here
 */
static cerr model_new_from_json(struct scene *scene, JsonNode *node, model_prep *mp)
{
    double speed = 0.75, roughness = -1.0, metallic = -1.0;
    char *name = NULL, *gltf = NULL;
    bool terrain_clamp = false, cull_face = true, alpha_blend = false, can_jump = false, can_dash = false;
    bool outline_exclude = false, can_levitate = false, nm_hazard = false;
    bool turns_with_ground = false, tilts_with_ground = false, bobs_head = false;
    JsonNode *p, *ent = nullptr, *ch = nullptr, *phys = nullptr, *anis = nullptr, *sfx = nullptr, *seqs = nullptr;
    double fade = -1.0;     /* the scene's "fade"; negative: the asset's, or none */
    int motion_segments = 8;
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
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "can_levitate"))
            can_levitate = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "turns_with_ground"))
            turns_with_ground = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "tilts_with_ground"))
            tilts_with_ground = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "bobs_head"))
            bobs_head = p->bool_;
        else if (p->tag == JSON_ARRAY && !strcmp(p->key, "entity"))
            ent = p->children.head;
        else if (p->tag == JSON_ARRAY && !strcmp(p->key, "character"))
            ch = p->children.head;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "animations"))
            anis = p;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "sequences"))
            seqs = p;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "fade"))
            fade = p->number_;
        else if (p->tag == JSON_OBJECT && !strcmp(p->key, "sfx"))
            sfx = p;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "speed"))
            speed = p->number_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "outline_exclude"))
            outline_exclude = p->bool_;
        else if (p->tag == JSON_BOOL && !strcmp(p->key, "nm_hazard"))
            nm_hazard = p->bool_;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "motion_segments"))
            motion_segments = p->number_;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "metallic"))
            metallic = p->number_;
        else if (p->tag == JSON_NUMBER && !strcmp(p->key, "roughness"))
            roughness = p->number_;
    }

    if (!name || !gltf) {
        dbg("json: name '%s' or gltf '%s' missing\n", name, gltf);
        return CERR_PARSE_FAILED;
    }

    LOCAL(model_prep, own);
    if (!mp) {
        own = CRES_RET_CERR(model_prep_new(scene, node));
        if (!IS_CERR(own->err)) own->err = model_prep_build(own, nullptr);
        mp = own;
    }

    if (IS_CERR(mp->err)) {
        err_cerr(mp->err, "Error loading GLTF '%s'\n", gltf);
        return mp->err;
    }

    LOCAL_SET(gltf_data, gd) = NOCU(mp->gd);

    /*
     * Pick the primary GLTF mesh (group). Multi-primitive meshes become
     * multiple model3dtx entries in the mq, one per primitive.
     */
    int primary_group = CRES_RET_CERR(gltf_primary_group(gd));
    int prim_first = CRES_RET_CERR(gltf_group_first(gd, primary_group));
    cres(int) rig_skin = scene_rig_skin(gd, prim_first);
    bool rig = !IS_CERR(rig_skin);

    LOCAL_SET(int, prims) = mem_alloc(sizeof(*prims), .nr = gltf_get_meshes(gd), .fatal_fail = 1);
    int prim_count = CRES_RET_CERR(scene_model_prims(gd, prims));

    for (int p = 0; p < prim_count; p++)
        CERR_RET_CERR(gltf_instantiate_one(gd, prims[p], &scene->mq, clap_get_pipeline(scene->clap_ctx)));

    /*
     * Walk back from mq's tail to recover the txms we just added: the
     * last prim_count entries correspond to the primitives in order. The
     * first entry is the "parent" whose model receives name/armature,
     * while per-model settings are applied to every primitive's txm.
     */
    model3dtx **prim_txms = mem_alloc(sizeof(*prim_txms), .nr = prim_count, .fatal_fail = 1);
    {
        model3dtx *cur = mq_model_last(&scene->mq);
        for (int p = prim_count - 1; p >= 0; p--) {
            prim_txms[p] = cur;
            if (p > 0)  cur = list_prev_entry(cur, entry);
        }
    }
    txm = prim_txms[0];

    /*
     * The rig's parts make one body: the parent's bounds take them all in,
     * for the culling and the collider sizing that read them.
     */
    for (int p = 1; rig && p < prim_count; p++) {
        model3d *part = prim_txms[p]->model;

        vec3_min(txm->model->aabb[0], txm->model->aabb[0], part->aabb[0]);
        vec3_max(txm->model->aabb[1], txm->model->aabb[1], part->aabb[1]);
    }

    auto n3d = clap_get_noise3d(scene->clap_ctx);
    for (int p = 0; p < prim_count; p++) {
        model3dtx *it = prim_txms[p];
        it->model->cull_face = cull_face;
        it->model->alpha_blend = alpha_blend;
        if (roughness >= 0.0)   it->mat.roughness = clampd(roughness, 0.0, 1.0);
        if (metallic >= 0.0)    it->mat.metallic = clampd(metallic, 0.0, 1.0);
        it->model->sfxc.on_add = scene->sfxc.on_add;
        it->model->sfxc.data = scene->sfxc.data;
        model3dtx_set_texture(it, UNIFORM_NOISE3D_TEX, noise3d_texture(n3d));
        if (scene->loader)  scene_loader_hold(scene->loader, it);
    }

    model3d_set_name(txm->model, name);

    struct scene_physics sp;
    scene_physics_parse(&sp, phys);

    /*
     * "armature": { "head": "<joint name>", ... } resolves gltf joint
     * names to semantic joint_type slots. These are consumed by
     * model3d_get_joint() (camera target, attach_joint, etc.) so
     * scenes can refer to joints by role without caring about how the
     * exporter named them.
     */
    model3d *model = txm->model;
    JsonNode *armature = json_find_member(node, "armature");
    if (armature && armature->tag == JSON_OBJECT) {
        for (unsigned int i = JOINT_NONE; i < JOINT_TYPE_MAX; i++) {
            auto jt_str = CRES_RET(joint_type_name(i), continue);

            JsonNode *jj = json_find_member(armature, jt_str);
            if (!jj || jj->tag != JSON_STRING)
                continue;

            const char *joint_name = jj->string_;
            for (unsigned int j = 0; j < model->nr_joints; j++)
                if (!strcmp(joint_name, model->joints[j].name)) {
                    model->joint_types[i] = j;
                    break;
                }
        }
    }

    /*
     * "look": { "yaw_max": <radians> | false, "pitch_max": ..., "slew": <rad/s> }
     * defines the head's look-at on the model over the characters' default
     * (character_look_cfg); false locks the turn: "yaw_max": false makes
     * a head that only nods.
     */
    JsonNode *look = json_find_member(node, "look");
    if (look && look->tag == JSON_OBJECT) {
        lookat_config cfg = character_look_cfg;

        for (p = look->children.head; p; p = p->next) {
            if (!strcmp(p->key, "yaw_max") && p->tag == JSON_NUMBER)            cfg.yaw_max = p->number_;
            else if (!strcmp(p->key, "yaw_max") && p->tag == JSON_BOOL)         cfg.no_yaw = !p->bool_;
            else if (!strcmp(p->key, "pitch_max") && p->tag == JSON_NUMBER)     cfg.pitch_max = p->number_;
            else if (!strcmp(p->key, "pitch_max") && p->tag == JSON_BOOL)       cfg.no_pitch = !p->bool_;
            else if (!strcmp(p->key, "slew") && p->tag == JSON_NUMBER)          cfg.slew = p->number_;
        }

        CRES_RET(anim_lib_pose_copy(model3d_anim_lib(model), &lookat_pose_ops, &cfg, sizeof(cfg)),
                 warn("%s: no look of its own, the default stands\n", name));
    }

    /*
     * "limbs": { "skip": [ "<joint name prefix>", ... ], "box": [ "<joint name>", ... ] }
     * makes a skinned model's rigid pieces its characters' limbs, a
     * collision shape on each joint (model3d::parts, the gltf loads with
     * them): the pieces of every primitive skinned with the model's skin,
     * a multi-mesh rig's parts among them, without the joints "skip"
     * names, with the pieces "box" names as their bounding boxes.
     */
    JsonNode *limbs = json_find_member(node, "limbs");
    if (limbs && limbs->tag == JSON_OBJECT && model->nr_joints) {
        JsonNode *skip = json_find_member(limbs, "skip");
        JsonNode *box = json_find_member(limbs, "box");
        JsonNode *it;

        cres(int) skin = gltf_mesh_skin(gd, prims[0]);

        for (int p = 1; !IS_CERR(skin) && p < prim_count; p++) {
            if (!prim_skinned_with(gd, prims[p], skin.val))     continue;

            CERR_RET(mesh_parts_merge(&model->parts, &prim_txms[p]->model->parts),
                     warn("%s: no limbs from '%s'\n", name, gltf_mesh_name(gd, prims[p])));
        }

        if (skip && skip->tag == JSON_ARRAY)
            json_foreach(it, skip)
                if (it->tag == JSON_STRING)     model3d_parts_skip(model, it->string_);

        if (box && box->tag == JSON_ARRAY)
            json_foreach(it, box) {
                if (it->tag != JSON_STRING)     continue;

                int joint = CRES_RET(model3d_joint_by_name(model, it->string_),
                                     { warn("%s: no joint '%s'\n", name, it->string_); continue; });
                CERR_RET(model3d_part_box(model, joint), warn("%s: no box for '%s'\n", name, it->string_));
            }
    }

    if (sfx)
        for (p = sfx->children.head; p; p = p->next)
            sfx_add_from_json(&txm->model->sfxc, clap_get_sound(scene->clap_ctx), p);

    cerr err = CERR_OK;

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
                entity3d_set(e, ENTITY3D_SKIP_CULLING, NULL);
                c->can_dash = can_dash;
                c->can_jump = can_jump;
                c->can_levitate = can_levitate;
                c->turns_with_ground = turns_with_ground;
                c->tilts_with_ground = tilts_with_ground;
                c->bobs_head = bobs_head;
            } else {
                e = ref_new(entity3d, .txmodel = txm);
            }
            if (gltf_mesh_fallback(gd, prim_first))
                entity3d_clear(e, ENTITY3D_VISIBLE);

            jpos = json_find_member(it, "outline_exclude");
            if (jpos && jpos->tag == JSON_BOOL)
                e->outline_exclude = jpos->bool_;
            else
                e->outline_exclude = outline_exclude;

            jpos = json_find_member(it, "nm_hazard");
            bool hazard = jpos && jpos->tag == JSON_BOOL ? jpos->bool_ : nm_hazard;
            if (hazard) entity3d_set(e, ENTITY3D_NM_HAZARD, nullptr);

            jpos = json_find_member(it, "name");
            if (jpos && jpos->tag == JSON_STRING)
                e->name = strdup(jpos->string_);

            /*
             * "attach": "<parent entity name>" makes this entity ride
             * the parent's skeleton. "attach_joint" picks which joint
             * (by semantic name) it binds to; without it, parent_joint
             * defaults to 0 — the root joint.
             */
            jpos = json_find_member(it, "attach");
            if (jpos && jpos->tag == JSON_STRING)
                e->parent = CRES_RET(mq_find_entity(&scene->mq, jpos->string_), continue);

            jpos = json_find_member(it, "attach_joint");
            if (jpos && jpos->tag == JSON_STRING && e->parent)
                e->parent_joint = CRES_RET(model3d_joint_by_type(e->parent->txmodel->model, jpos->string_), continue);

            /*
             * "rotate": [rx, ry, rz] in degrees — sets the entity's
             * initial orientation. Applied before the parent-joint
             * transform when the entity is attached, so it lives in
             * the parent joint's local space in that case.
             */
            jpos = json_find_member(it, "rotate");
            if (jpos && jpos->tag == JSON_ARRAY) {
                double _angles[3];
                if (!json_double_array(jpos, _angles, 3)) {
                    vec3 angles = { _angles[0], _angles[1], _angles[2] };
                    transform_set_angles(&e->xform, angles, true);
                }
            }

            jpos = json_find_member(it, "position");
            if (!jpos || jpos->tag != JSON_ARRAY)
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

            jpos = json_find_member(it, "light_cutoff");
            if (jpos && jpos->tag == JSON_NUMBER && e->light_idx >= 0) {
                light_set_cutoff(&scene->light, e->light_idx, to_radians(jpos->number_));
                light_set_directional(&scene->light, e->light_idx, true);
            }

            jpos = json_find_member(it, "light_volumetric");
            if (jpos && jpos->tag == JSON_NUMBER && e->light_idx >= 0)
                light_set_volumetric(&scene->light, e->light_idx, jpos->number_);

light_done:
            if (e->light_idx >= 0) {
                e->light = &scene->light;
                entity3d_set(e, ENTITY3D_LIGHT_SOURCE, NULL);
                /* default_update() fires it, with the entity's transform for the frame */
                light_set_update(&scene->light, e->light_idx,
                                 light_update_from_entity, e, NULL, NULL);
                light_update_from_entity(&scene->light, e->light_idx, e);
            }

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

            mat4x4_identity(e->mx);
            transform_translate_mat4x4(&e->xform, e->mx);
            transform_rotate_mat4x4(&e->xform, e->mx);
            mat4x4_scale_aniso(e->mx, e->mx, e->scale, e->scale, e->scale);

            if (phys) {
                scene_entity_add_physics(scene, e, sp.mass, sp.class, sp.type, sp.geom_off,
                                         sp.geom_radius, sp.geom_length, sp.bounce, sp.bounce_vel);
                if (c && e->phys_body)
                    phys_body_enable(e->phys_body, false);
            }

            /* a character's parts move with it: no static colliders left at the spawn point */
            scene_spawn_prim_children(scene, e, gd, prim_txms, prims, prim_count, !!phys && !c);

            if (entity_animated(e)) {
                for (p = anis ? anis->children.head : nullptr; p; p = p->next) {
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

                /*
                 * What the character plays in each state, from the scene's
                 * "sequences", else the asset's clap::anim::* extras, else
                 * the default table; the clips have their semantic names
                 * now. Once per model: every character of it shares the
                 * library.
                 */
                if (c) {
                    anim_lib_t *lib = model3d_anim_lib(e->txmodel->model);

                    if (!anim_lib_nr_transitions(lib)) {
                        cerr err = seqs ?
                            anim_lib_transitions_from_json(lib, seqs, max(fade, 0.0), character_states, CS_NR) :
                            anim_lib_transitions_from_gltf(lib, gd, fade, character_states, CS_NR);

                        if (!seqs && IS_CERR_CODE(err, CERR_NOT_FOUND))
                            err = character_anims_baked(lib, max(fade, 0.0));
                        CERR_RET(err, warn("'%s': animations: %s\n", name, cerr_str(__cerr.err)));
                    }
                }

                if (c &&
                    animation_by_name(e->txmodel->model, "start") >= 0 &&
                    animation_by_name(e->txmodel->model, "start_to_idle") >= 0)
                    CERR_RET(gpsm_set(&c->sm, CS_START),
                             err_cerr(__cerr, "'%s' can't start asleep\n", entity_name(e)));
            }
        }
    } else {
        struct clap_config *clap_cfg = clap_get_config(scene->clap_ctx);
        struct instantiator *instor, *iter;
        entity3d *e;
        int nr_nodes = gltf_get_nodes(gd), ni, spawned = 0;

        /*
         * Fallback path when neither "entity" nor "character" arrays are
         * present in the scene JSON: instantiate one entity per GLTF node
         * that references the primary mesh, using its TRS directly.
         */
        for (ni = 0; ni < nr_nodes; ni++) {
            if (CRES_RET(gltf_node_mesh(gd, ni), continue) != primary_group)    continue;

            vec3 t, s;
            quat r;
            CERR_RET(gltf_node_translation(gd, ni, t), continue);
            CERR_RET(gltf_node_rotation(gd, ni, r), continue);
            CERR_RET(gltf_node_scale(gd, ni, s), continue);

            /* an entity's scale is one number, the node's first: the other two go */
            if (s[1] != s[0] || s[2] != s[0]) {
                cresp(const_char) node_name = gltf_node_name(gd, ni);

                warn("'%s': node '%s' scale (%g, %g, %g) isn't uniform: only %g applies\n", name,
                     IS_CERR(node_name) ? "?" : node_name.val, s[0], s[1], s[2], s[0]);
            }

            e = ref_new(entity3d, .txmodel = txm);
            if (gltf_mesh_fallback(gd, prim_first))
                entity3d_clear(e, ENTITY3D_VISIBLE);
            entity3d_position(e, t);
            entity3d_scale(e, s[0]);
            transform_set_quat(&e->xform, r);

            if (terrain_clamp)
                phys_ground_entity(clap_get_phys(scene->clap_ctx), e);

            mat4x4_identity(e->mx);
            transform_translate_mat4x4(&e->xform, e->mx);
            transform_rotate_mat4x4(&e->xform, e->mx);
            mat4x4_scale_aniso(e->mx, e->mx, e->scale, e->scale, e->scale);

            /*
             * Physics is opt-in: the model's JSON "physics" block enables it
             * for every instance, with GEOM_TRIMESH/PHYS_GEOM defaults, and
             * the node's clap::physics::* extras enable and/or override it
             * per instance.
             */
            bool n_phys = phys || gltf_node_extras_present(gd, ni, "clap::physics::");

            geom_class   n_class   = sp.class;
            phys_type    n_ptype   = sp.type;
            double       n_mass    = sp.mass;
            double       n_off     = sp.geom_off;
            double       n_radius  = sp.geom_radius;
            double       n_length  = sp.geom_length;
            double       n_bounce  = sp.bounce;
            double       n_bv      = sp.bounce_vel;
            const char   *ex_s;
            double       ex_n;

            if (gltf_node_extras_string(gd, ni, "clap::physics::geom", &ex_s)) {
                if (!strcmp(ex_s, "trimesh"))       n_class = GEOM_TRIMESH;
                else if (!strcmp(ex_s, "sphere"))   n_class = GEOM_SPHERE;
                else if (!strcmp(ex_s, "capsule"))  n_class = GEOM_CAPSULE;
            }
            if (gltf_node_extras_string(gd, ni, "clap::physics::type", &ex_s)) {
                if (!strcmp(ex_s, "body"))      n_ptype = PHYS_BODY;
                else if (!strcmp(ex_s, "geom")) n_ptype = PHYS_GEOM;
            }
            if (gltf_node_extras_number(gd, ni, "clap::physics::mass", &ex_n))       n_mass   = ex_n;
            if (gltf_node_extras_number(gd, ni, "clap::physics::yoffset", &ex_n))    n_off    = ex_n;
            if (gltf_node_extras_number(gd, ni, "clap::physics::radius", &ex_n))     n_radius = ex_n;
            if (gltf_node_extras_number(gd, ni, "clap::physics::length", &ex_n))     n_length = ex_n;
            if (gltf_node_extras_number(gd, ni, "clap::physics::bounce", &ex_n))     n_bounce = ex_n;
            if (gltf_node_extras_number(gd, ni, "clap::physics::bounce_vel", &ex_n)) n_bv     = ex_n;

            if (n_phys)
                scene_entity_add_physics(scene, e, n_mass, n_class, n_ptype, n_off,
                                         n_radius, n_length, n_bounce, n_bv);

            scene_spawn_prim_children(scene, e, gd, prim_txms, prims, prim_count, n_phys);

            // scene_bv_update(scene, e);

            spawned++;

            /* the game's turn to read what else the node says, see scene_node_spawn_fn */
            if (!clap_cfg->node_spawn)  continue;

            CERR_RET(clap_cfg->node_spawn(scene, gd, ni, e, clap_cfg->node_spawn_priv), { err = __cerr; break; });
        }

        if (!spawned) {
            list_for_each_entry_iter(instor, iter, &scene->instor, entry) {
                if (!strcmp(txmodel_name(txm), instor->name)) {
                    e = instantiate_entity(txm, instor, true, 0.5, scene);
                    if (gltf_mesh_fallback(gd, prim_first))
                        entity3d_clear(e, ENTITY3D_VISIBLE);
                    list_del(&instor->entry);
                    free(instor);

                    if (nm_hazard)  entity3d_set(e, ENTITY3D_NM_HAZARD, nullptr);

                    if (phys)
                        scene_entity_add_physics(scene, e, sp.mass, sp.class, sp.type, sp.geom_off,
                                                 sp.geom_radius, sp.geom_length, sp.bounce, sp.bounce_vel);

                    scene_spawn_prim_children(scene, e, gd, prim_txms, prims, prim_count, !!phys);
                }
            }
        }
    }

    mem_free(prim_txms);

    CERR_RET_CERR(err);

    dbg("loaded model '%s'\n", name);

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
    light_set_directional(&s->light, idx, true);

    float fpos[3] = { pos[0], pos[1], pos[2] };
    float fcolor[3] = { color[0], color[1], color[2] };
    light_set_pos(&s->light, idx, fpos);
    light_set_color(&s->light, idx, fcolor);

    double fdir[3];
    jpos = json_find_member(light, "direction");
    if (jpos && jpos->tag == JSON_ARRAY) {
        if (!json_double_array(jpos, fdir, 3)) {
            float dir[3] = { fdir[0], fdir[1], fdir[2] };
            light_set_direction(&s->light, idx, dir);
        }
    }

    double _light_attenuation[3];
    jpos = json_find_member(light, "attenuation");
    if (jpos && jpos->tag == JSON_ARRAY) {
        if (!json_double_array(jpos, _light_attenuation, 3)) {
            float light_attenuation[3] = { _light_attenuation[0], _light_attenuation[1], _light_attenuation[2] };
            light_set_attenuation(&s->light, idx, light_attenuation);
            light_set_directional(&s->light, idx, false);
        }
    }

    jpos = json_find_member(light, "cutoff");
    if (jpos && jpos->tag == JSON_NUMBER) {
        light_set_cutoff(&s->light, idx, jpos->number_);
        light_set_directional(&s->light, idx, true);
    }

    jpos = json_find_member(light, "volumetric");
    if (jpos && jpos->tag == JSON_NUMBER)   light_set_volumetric(&s->light, idx, jpos->number_);

    bool spot = light_is_spotlight(&s->light, idx);

    /*
     * A directional light without an explicit direction shines at the
     * origin from its position, at any slot -- the sun doesn't stop
     * having a direction when a shadow-casting spotlight bumps it from
     * slot 0
     */
    if (!spot && !json_find_member(light, "direction")) {
        vec3 center = {};
        vec3_sub(&s->light.dir[idx * 3], center, &s->light.pos[idx * 3]);
    }

    /*
     * Seed the CSM frustum for a slot-0 non-spotlight directional
     * light; scene_cameras_calc() will refresh it per-frame. A static
     * spotlight gets its shadow view built here once -- shadows only
     * render from slot 0, but the view is cheap and correct at any
     * index; spotlights that track an entity or a camera refresh
     * theirs from the light_update_*() callbacks instead.
     */
    if (idx == 0 && !spot) {
        bool shadow_vsm = clap_get_render_options(s->clap_ctx)->shadow_vsm;
        view_update_from_frustum(s->clap_ctx, &s->light.view[idx], &s->camera[0].view, &s->light.dir[idx * 3], 0.0, !shadow_vsm);
    } else if (spot) {
        light_spot_view_update(s->clap_ctx, &s->light, idx);
    }

    return CERR_OK;
}

/*
 * Scene-wide "navmesh" block: build parameters for the walkable mesh.
 * max_slope is authored in degrees (converted here); the rest are metres,
 * matching navmesh_params. Any omitted field keeps navmesh_new()'s default.
 */
static cerr scene_parse_navmesh(struct scene *s, JsonNode *nm)
{
    if (nm->tag != JSON_OBJECT) return CERR_INVALID_FORMAT;

    JsonNode *p;
    for (p = nm->children.head; p; p = p->next) {
        if (p->tag != JSON_NUMBER)  continue;
        if (!strcmp(p->key, "max_slope"))
            s->nm_params.max_slope = to_radians(p->number_);
        else if (!strcmp(p->key, "weld"))
            s->nm_params.weld = p->number_;
        else if (!strcmp(p->key, "step_height"))
            s->nm_params.step_height = p->number_;
        else if (!strcmp(p->key, "agent_height"))
            s->nm_params.agent_height = p->number_;
        else if (!strcmp(p->key, "agent_radius"))
            s->nm_params.agent_radius = p->number_;
        else if (!strcmp(p->key, "subdivide"))
            s->nm_params.subdivide = p->number_;
    }

    s->nm_wanted = true;

    return CERR_OK;
}

static void scene_onload(struct lib_handle *h, void *buf)
{
    struct scene *scene = buf;
    char         msg[256];

    if (h->state == RES_ERROR) {
        err("couldn't load scene %s\n", h->name);
        goto out;
    }

    scene->json_root = json_decode(h->buf);
    if (!scene->json_root) {
        err("couldn't parse '%s'\n", h->name);
        goto err;
    }

    if (!json_check(scene->json_root, msg)) {
        err("error parsing '%s': '%s'\n", h->name, msg);
        goto err;
    }

    if (scene->json_root->tag != JSON_OBJECT) {
        err("parse error in '%s'\n", h->name);
        goto err;
    }

out:
    scene->file_name = lib_figure_uri(h->type, h->name);
    ref_put(h);

    return;

err:
    h->state = RES_ERROR;
    ref_put(h);
    json_delete(scene->json_root);
    scene->json_root = NULL;
}

/* The entries of a scene file, by what they put in the scene */
enum scene_item_kind {
    SCENE_ITEM_LIGHT,
    SCENE_ITEM_SFX,
    SCENE_ITEM_MODEL,
    /* the "navmesh" block's bake, over everything that went in before it */
    SCENE_ITEM_NAVMESH,
};

struct scene_item {
    JsonNode                *node;
    /* a model's glTF, given to the loader thread ahead of its turn */
    model_prep              *prep;
    enum scene_item_kind    kind;
};

/* A scene file on its way into the scene, an entry at a time */
struct scene_loader {
    darray(struct scene_item, items);
    /* the models it put in, kept out of the frames until it's over */
    darray(model3dtx *, held);
    /* scene_load_async()'s: the end of the load, and its data */
    scene_loaded_fn done;
    void            *data;
    struct timespec started;
    /* the next entry to go in */
    unsigned int    next;
    /* the next entry to read and give to the loader thread */
    unsigned int    next_prep;
    /* models given to the loader thread and not in yet */
    unsigned int    nr_ahead;
    unsigned int    nr_models;
    unsigned int    nr_models_added;
    /* the frames the load took, and the longest step one of them made */
    unsigned int    nr_frames;
    unsigned long   longest_step_us;
};

/*
 * Read and parse the scene file and list its entries. They go in in the
 * order the file has them: entity lights take the lowest free light slot,
 * terrain_clamp casts rays at what came before, and "attach" looks for its
 * parent among the entities that are in.
 */
static cerr scene_loader_begin(struct scene *scene, struct scene_loader *sl, const char *name)
{
    darray_init(sl->items);
    darray_init(sl->held);

    struct lib_handle *lh = lib_request(RES_ASSET, name, scene_onload, scene);
    if (!lh) {
        err("couldn't read scene %s\n", name);
        return CERR_SCENE_NOT_LOADED;
    }

    ref_put_last(lh);
    if (!scene->json_root)  return CERR_SCENE_NOT_LOADED;

    for (JsonNode *p = scene->json_root->children.head; p; p = p->next) {
        enum scene_item_kind kind;

        if (!strcmp(p->key, "name")) {
            if (p->tag != JSON_STRING) {
                err("parse error in '%s'\n", name);
                return CERR_PARSE_FAILED;
            }
            strncpy(scene->name, p->string_, sizeof(scene->name));
            continue;
        } else if (!strcmp(p->key, "model")) {
            if (p->tag != JSON_ARRAY) {
                err("parse error in '%s'\n", name);
                return CERR_PARSE_FAILED;
            }
            kind = SCENE_ITEM_MODEL;
            sl->nr_models += json_nr_children(p);
        } else if (!strcmp(p->key, "light") && p->tag == JSON_ARRAY) {
            kind = SCENE_ITEM_LIGHT;
        } else if (!strcmp(p->key, "sfx") && p->tag == JSON_OBJECT) {
            kind = SCENE_ITEM_SFX;
        } else if (!strcmp(p->key, "navmesh")) {
            CERR_RET_CERR(scene_parse_navmesh(scene, p));
            continue;
        } else {
            continue;
        }

        for (JsonNode *e = p->children.head; e; e = e->next) {
            struct scene_item *item = darray_add(sl->items);
            if (!item)  return CERR_NOMEM;

            *item = (struct scene_item){ .node = e, .kind = kind };
        }
    }

    /* every entity is in by then: the navmesh bakes over the whole scene */
    if (scene->nm_wanted) {
        struct scene_item *item = darray_add(sl->items);
        if (!item)  return CERR_NOMEM;

        *item = (struct scene_item){ .kind = SCENE_ITEM_NAVMESH };
    }

    return CERR_OK;
}

/* Put the scene file's next entry in the scene */
static cerr scene_loader_add_next(struct scene *scene, struct scene_loader *sl)
{
    struct scene_item *item = DA(sl->items, sl->next++);

    switch (item->kind) {
        case SCENE_ITEM_LIGHT:
            return scene_add_light_from_json(scene, item->node);

        case SCENE_ITEM_SFX:
            /* a sound that doesn't load doesn't stop the scene */
            sfx_add_from_json(&scene->sfxc, clap_get_sound(scene->clap_ctx), item->node);
            return CERR_OK;

        case SCENE_ITEM_MODEL: {
            LOCAL_SET(model_prep, mp) = NOCU(item->prep);
            if (mp) sl->nr_ahead--;

            CERR_RET_CERR(model_new_from_json(scene, item->node, mp));
            sl->nr_models_added++;
            return CERR_OK;
        }

        case SCENE_ITEM_NAVMESH:
            return scene_navmesh_init(scene);
    }

    return CERR_INVALID_ARGUMENTS;
}

/*
 * Keep a model that scene_load_async() put in out of the frames until the
 * load is over: what the scene had before the load, a menu backdrop, say,
 * goes on being drawn
 */
static void scene_loader_hold(struct scene_loader *sl, model3dtx *txm)
{
    model3dtx **held = darray_add(sl->held);

    /* one that can't be held back is drawn as it comes in */
    if (!held)  return;

    *held = txm;
    txm->loading = true;
}

/* The scene file is all in, or @err stopped it: hand the control over, or drop the file */
static void scene_loader_end(struct scene *scene, struct scene_loader *sl, cerr err)
{
    struct scene_item *item;
    model3dtx **txm;

    /* what's in is drawn from now on, the whole file or as much as went in */
    darray_for_each(txm, sl->held)
        (*txm)->loading = false;
    darray_clearout(sl->held);

    /* what the loader thread prepared and nobody took */
    darray_for_each(item, sl->items) {
        model_prep *mp = item->prep;

        if (!mp)    continue;

        /* still on the loader thread: its done() frees it */
        if (mp->job) {
            mp->abandoned = true;
            job_cancel(mp->job);
        } else {
            model_prep_free(mp);
        }
    }
    darray_clearout(sl->items);

    if (IS_CERR(err)) {
        json_delete(scene->json_root);
        scene->json_root = nullptr;
        free(scene->file_name);
        scene->file_name = nullptr;
        return;
    }

    dbg("loaded scene: '%s'\n", scene->name);
    scene_control_next(scene);
}

/* Give the loader thread the models up to SCENE_LOAD_AHEAD ahead of the add step */
static void scene_loader_submit(struct scene *scene, struct scene_loader *sl)
{
    if (!scene->loader_pool)    return;

    for (; sl->next_prep < darray_count(sl->items) && sl->nr_ahead < SCENE_LOAD_AHEAD; sl->next_prep++) {
        struct scene_item *item = DA(sl->items, sl->next_prep);

        if (item->kind != SCENE_ITEM_MODEL) continue;

        item->prep = model_prep_submit(scene, item->node);
        if (item->prep) sl->nr_ahead++;
    }
}

/*
 * Take what the loader thread has prepared and put the scene file's entries
 * in, up to the next model that isn't prepared yet, and no more than one
 * model a frame
 */
static void scene_load_step(struct scene *scene)
{
    struct scene_loader *sl = scene->loader;
    struct timespec start, end, step;

    /* the step's time takes in the poll and the refill: all the main thread gives the load */
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (scene->loader_pool) worker_pool_poll(scene->loader_pool, 0);
    if (!sl)                return;

    cerr err = CERR_OK;
    while (sl->next < darray_count(sl->items)) {
        struct scene_item *item = DA(sl->items, sl->next);
        bool model = item->kind == SCENE_ITEM_MODEL;

        if (model && item->prep && !item->prep->delivered)  break;

        err = scene_loader_add_next(scene, sl);
        // if (IS_CERR(err))   continue;

        if (model) {
            if (scene->ls)  loading_screen_set_progress(scene->ls, (float)sl->nr_models_added / (float)sl->nr_models);
            break;
        }
    }

    bool over = /*IS_CERR(err) || */sl->next >= darray_count(sl->items);
    if (!over && !IS_CERR(err)) scene_loader_submit(scene, sl);

    clock_gettime(CLOCK_MONOTONIC, &end);
    timespec_diff(&start, &end, &step);
    sl->longest_step_us = max(sl->longest_step_us, (unsigned long)(step.tv_sec * 1000000 + step.tv_nsec / 1000));
    sl->nr_frames++;

    if (!over)  return;

    timespec_diff(&sl->started, &end, &step);
    dbg("scene file took %lu ms over %u frames, the longest step %lu us\n",
        (unsigned long)(step.tv_sec * 1000 + step.tv_nsec / 1000000), sl->nr_frames, sl->longest_step_us);

    /* the load is over before done() runs, which may start another */
    scene_loaded_fn done = sl->done;
    void *data = sl->data;

    scene->loader = nullptr;
    scene_loader_end(scene, sl, err);
    mem_free(sl);

    if (done)   done(scene, err, data);
}

cresp(worker_pool) scene_loader_pool(struct scene *scene)
{
    if (scene->loader_pool) return cresp_val(worker_pool, scene->loader_pool);

    scene->loader_pool = CRES_RET_T(worker_pool_new(.ctx = scene->clap_ctx, .name = "scene loader", .nr_threads = 1),
                                    worker_pool);

    return cresp_val(worker_pool, scene->loader_pool);
}

cerr scene_load_async(struct scene *scene, const char *name, scene_loaded_fn done, void *data)
{
    if (scene->json_root)   return CERR_ALREADY_LOADED;

    struct scene_loader *sl = mem_alloc(sizeof(*sl), .zero = 1);
    if (!sl)    return CERR_NOMEM;

    CERR_RET(scene_loader_begin(scene, sl, name), {
        scene_loader_end(scene, sl, __cerr);
        mem_free(sl);
        return CERR_SCENE_NOT_LOADED;
    });

    cresp(worker_pool) res = scene_loader_pool(scene);
    if (IS_CERR(res))   err_cerr(res, "no loader thread: the frames prepare the models\n");

    sl->done = done;
    sl->data = data;
    clock_gettime(CLOCK_MONOTONIC, &sl->started);
    scene->loader = sl;

    dbg("loading '%s': %u models, prepared on %s\n", name, sl->nr_models,
        scene->loader_pool ? "the loader thread" : "the frames");
    scene_loader_submit(scene, sl);

    return CERR_OK;
}

bool scene_is_loading(struct scene *scene)
{
    return !!scene->loader;
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

    struct scene_loader sl = {};
    cerr err = scene_loader_begin(scene, &sl, name);

    while (!IS_CERR(err) && sl.next < darray_count(sl.items)) {
        bool model = DA(sl.items, sl.next)->kind == SCENE_ITEM_MODEL;

        err = scene_loader_add_next(scene, &sl);
        if (!IS_CERR(err) && model && scene->ls)
            loading_screen_progress(scene->ls, (float)sl.nr_models_added / (float)sl.nr_models);
    }

    scene_loader_end(scene, &sl, err);

    return IS_CERR(err) ? CERR_SCENE_NOT_LOADED : CERR_OK;
}

void scene_done(struct scene *scene)
{
    struct scene_loader *sl = scene->loader;
    struct instantiator *instor;
    struct character *iter, *ch;

    if (sl) {
        dbg("stopping the load at entry %u of %zu\n", sl->next, darray_count(sl->items));
        scene->loader = nullptr;
        scene_loader_end(scene, sl, CERR_CANCELLED);
        mem_free(sl);
    }

    /* the thread finishes what's queued: cancelled jobs don't run, and their done() frees them */
    if (scene->loader_pool) ref_put_last(scene->loader_pool);

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

    if (scene->nm) {
        navmesh_free(scene->nm);
        scene->nm = nullptr;
    }

    mq_release(&scene->mq);

    /*
     * entities may hold references to lights, so this has to happen after
     * mq_release()
     */
    light_done(scene->clap_ctx, &scene->light);

    for (int i = 0; i < scene->nr_cameras; i++) darray_clearout(scene->cameras[i].shakes);
}
