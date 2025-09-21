// SPDX-License-Identifier: Apache-2.0
#include "scene.h"
#include "shader.h"
#include "pipeline.h"
#include "pipeline-debug.h"
#include "pipeline-internal.h"
#include "ui-debug.h"

void pipeline_debug_init(pipeline *pl)
{
    darray_init(pl->dropdown);
}

void pipeline_debug_done(pipeline *pl)
{
    darray_clearout(pl->dropdown);
}

void pipeline_dropdown_push(pipeline *pl, render_pass *pass)
{
    pipeline_dropdown *entry;

    for (int i = 0; i < pass->nr_sources; i++) {
        if (!pass->blit_fbo[i] && !pass->use_tex[i])
            continue;

        entry = darray_add(pl->dropdown);
        if (!entry)
            return;

        snprintf(entry->name, sizeof(entry->name), "%s input %d", pass->name, i);
        entry->tex = pass->blit_fbo[i] ?
            fbo_texture(pass->blit_fbo[i], FBO_COLOR_TEXTURE(0)) :
            pass->use_tex[i];
    }

    if (pass->cascade >= 0 && !pass->quad) {
        entry = darray_add(pl->dropdown);
        if (!entry)
            return;

        snprintf(entry->name, sizeof(entry->name), "%s cascade %d", pass->name, pass->cascade);
        entry->tex = fbo_texture(
            pass->fbo,
            fbo_attachment_valid(pass->fbo, FBO_COLOR_TEXTURE(0)) ?
                FBO_COLOR_TEXTURE(0) :
                FBO_DEPTH_TEXTURE(0)
        );
        entry->pass = pass;
    } else {
        if (fbo_attachment_valid(pass->fbo, FBO_DEPTH_TEXTURE(0))) {
            entry = darray_add(pl->dropdown);
            if (!entry)
                return;

            snprintf(entry->name, sizeof(entry->name), "%s depth", pass->name);
            entry->tex = fbo_texture(pass->fbo, FBO_DEPTH_TEXTURE(0));
            entry->pass = pass;
        }

        for (int i = 0; i < FBO_COLOR_ATTACHMENTS_MAX; i++) {
            if (fbo_attachment_valid(pass->fbo, FBO_COLOR_TEXTURE(i))) {
                entry = darray_add(pl->dropdown);
                if (!entry)
                    return;

                snprintf(entry->name, sizeof(entry->name), "%s color %d", pass->name, i);
                entry->tex = fbo_texture(pass->fbo, FBO_COLOR_TEXTURE(i));
                entry->pass = pass;
            } else {
                break;
            }
        }
    }
}

void pipeline_debug_begin(struct pipeline *pl)
{
    debug_module *dbgm = ui_igBegin_name(DEBUG_PIPELINE_PASSES, ImGuiWindowFlags_AlwaysAutoResize,
                                         "pipeline %s", pl->name);

    if (!dbgm->display || !dbgm->unfolded)
        return;

    igBeginTable("pipeline passes", 7, ImGuiTableFlags_Borders, (ImVec2){0,0}, 0);
    igTableSetupColumn("pass", ImGuiTableColumnFlags_WidthStretch, 0, 0);
    igTableSetupColumn("method", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("src", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("dim", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("at", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("culled", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableHeadersRow();
}

void pipeline_debug_end(struct pipeline *pl)
{
    debug_module *dbgm = ui_debug_module(DEBUG_PIPELINE_PASSES);

    if (!dbgm->display)
        return;
    if (dbgm->unfolded)
        igEndTable();

    ui_igEnd(DEBUG_PIPELINE_PASSES);
}

static const char *render_method_string[] = {
    [RM_BLIT]   = "blit",
    [RM_USE]    = "use",
    [RM_PLUG]   = "plug",
    [RM_RENDER] = "render",
};

void pipeline_pass_debug_begin(struct pipeline *pl, struct render_pass *pass, int srcidx)
{
    debug_module *dbgm = ui_debug_module(DEBUG_PIPELINE_PASSES);
    render_source *rsrc = &pass->source[srcidx];

    if (!dbgm->display || !dbgm->unfolded)
        return;
    igTableNextRow(0, 0);
    igTableNextColumn();

    /* "pass" */
    if (!srcidx)
        igText("%s %dx%d", pass->name, fbo_width(pass->fbo), fbo_height(pass->fbo));
    igTableNextColumn();

    /* "method" */
    igText("%ss", render_method_string[rsrc->method]);
    igTableNextColumn();

    /* "src" */
    switch (rsrc->method) {
        case RM_BLIT:
        case RM_USE:
            igText("%s:%s", rsrc->pass->name, fbo_attachment_string(rsrc->attachment));
            break;
        case RM_RENDER:
            igText("<mq>");
            break;
        default:
            break;
    }
    igTableNextColumn();

    /* "at" */
    if (rsrc->method == RM_BLIT || rsrc->method == RM_USE)
        igText("%s", shader_get_var_name(rsrc->sampler));
    igTableNextColumn();

    /* "dim" */
    fbo_t *src_fbo = pass->blit_fbo[srcidx];
    texture_t *tex = pass->use_tex[srcidx];
    if (src_fbo) {
        igText("%ux%u", fbo_width(src_fbo), fbo_height(src_fbo));
    } else if (tex) {
        unsigned int width, height;
        texture_get_dimesnions(tex, &width, &height);
        igText("%ux%u", width, height);
    }
    igTableNextColumn();
}

void pipeline_pass_debug_end(struct pipeline *pl, unsigned long count, unsigned long culled)
{
    debug_module *dbgm = ui_debug_module(DEBUG_PIPELINE_PASSES);
    if (!dbgm->display || !dbgm->unfolded)
        return;

    igText("%lu", count);
    igTableNextColumn();
    igText("%lu", culled);
}

static void pipeline_passes_dropdown(struct pipeline *pl, int *item, texture_t **tex, render_pass **pass)
{
    pipeline_dropdown *entry;
    int i = 0;

    if (igBeginCombo("passes", DA(pl->dropdown, *item)->name, ImGuiComboFlags_HeightLargest)) {
        darray_for_each(entry, pl->dropdown) {
            bool selected = *item == i;

            igPushID_Int(i);
            if (igSelectable_Bool(entry->name, selected, selected ? ImGuiSelectableFlags_Highlight : 0, (ImVec2){0, 0})) {
                igSetItemDefaultFocus();
                *item = i;
            }
            igPopID();

            i++;
        }
        igEndCombo();
    }

    *tex = DA(pl->dropdown, *item)->tex;
    *pass = DA(pl->dropdown, *item)->pass;
}

void pipeline_debug(struct pipeline *pl)
{
    debug_module *dbgm = ui_igBegin(DEBUG_PIPELINE_SELECTOR, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    unsigned int width, height;
    texture_t *pass_tex = NULL;
    render_pass *pass = NULL;
    static int pass_preview;
    int depth_log2;

    if (!dbgm->unfolded)
        goto ui_ig_end;

    pipeline_passes_dropdown(pl, &pass_preview, &pass_tex, &pass);
    if (pass_tex) {
        texture_get_dimesnions(pass_tex, &width, &height);
        /* If square with a power of 2 side, enable resize slider */
        if (pass && width == height && !(width & (width - 1))) {
            int prev_depth_log2 = depth_log2 = ffs(width) - 1;
            igSliderInt("dim log2", &depth_log2, 8, 16, "%d", 0);
            if (depth_log2 != prev_depth_log2) {
                width = height = 1 << depth_log2;
                RENDER_PASS_OPS_PARAMS(pl, pass);
                pass->ops->resize(&params, &width, &height);
                cerr err = fbo_resize(pass->fbo, width, height);
                /*
                 * On failure, fbo_resize() will try to revert to the original size,
                 * if that also fails, it will return an error
                 */
                if (IS_CERR(err))
                    err_cerr(err, "pass '%s' error resizing to %u x %u\n",
                             pass->name, width, height);
            }
            igText("shadow map resolution: %d x %d", width, height);
        } else {
            igText("texture resolution: %d x %d", width, height);
        }
    }

ui_ig_end:
    ui_igEnd(DEBUG_PIPELINE_SELECTOR);

    if (pass_tex && !texture_is_array(pass_tex) && !texture_is_multisampled(pass_tex)) {
        if (igBegin("Render pass preview", NULL, 0)) {
            ImVec2 avail = igGetContentRegionAvail();
            if (avail.x < 512) {
                igPushItemWidth(512);
                avail.x = 512;
            }
            double aspect = (double)height / width;
            ImTextureRef *tex_ref = ImTextureRef_ImTextureRef_TextureID((ImTextureID)texture_id(pass_tex));
#ifdef CONFIG_ORIGIN_TOP_LEFT
            igImage(*tex_ref, (ImVec2){avail.x, avail.x * aspect}, (ImVec2){0,0}, (ImVec2){1,1});
#else
            igImage(*tex_ref, (ImVec2){avail.x, avail.x * aspect}, (ImVec2){0,1}, (ImVec2){1,0});
#endif /* !CONFIG_ORIGIN_TOP_LEFT */
            ImTextureRef_destroy(tex_ref);
            igEnd();
        } else {
            igEnd();
        }
    }
}
