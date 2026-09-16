// SPDX-License-Identifier: Apache-2.0
/*
 * Navmesh debug visualization: a wireframe overlay of the walkable surface,
 * the roamer debug window and the path/radius overlays.
 *
 * This reaches into the scene/render/UI stack, which the rest of the navmesh
 * deliberately does not. It lives in its own compilation unit, built only for
 * non-final configurations (gated in core/nav/CMakeLists.txt); final and test
 * builds link neither this file nor the dependencies it pulls in.
 */
#include "common.h"
#include "memory.h"
#include "messagebus.h"
#include "util.h"
#include "clap.h"
#include "mesh.h"
#include "pipeline.h"
#include "render.h"
#include "scene.h"
#include "shader.h"
#include "ui-debug.h"
#include "navmesh.h"
#include "navmesh-internal.h"
#include "nav/roamer.h"

/*
 * Sit just above the ground the overlay mirrors - and well under a
 * riser: a lift of a full riser posts each tread's lines level with the
 * next step's solid, and the overlay dashes out on every staircase.
 */
#define NM_OVERLAY_LIFT     0.04f
#define NM_OVERLAY_VERTS    65536   /* one 16-bit index run's worth of corners */

/*
 * One overlay chunk: a line-topology entity from the local vertex and
 * edge runs, hidden until the debug checkbox shows it. The scratch
 * buffers are LOCAL()s, mem_free()'d at scope exit so every error path
 * here carries no cleanup.
 */
static cerr nm_overlay_emit(navmesh *nm, struct scene *scene, const float *vx,
                            size_t nr_vx, const unsigned short *lines, size_t nr_lines)
{
    /* flat-up normals and zero UVs, to satisfy the model shader */
    LOCAL_SET(void, norm) = mem_alloc(sizeof(float) * 3, .nr = nr_vx, .zero = 1);
    LOCAL_SET(void, tx)   = mem_alloc(sizeof(float) * 2, .nr = nr_vx, .zero = 1);

    if (!norm || !tx)   return CERR_NOMEM;

    float *normf = norm;
    for (size_t i = 0; i < nr_vx; i++)  normf[i * 3 + 1] = 1.0f;

    LOCAL_SET(mesh_t, m) = CRES_RET_CERR(ref_new_checked(mesh, .name = "navmesh"));
    CERR_RET_CERR(mesh_attr_dup(m, MESH_VX,   (void *)vx, sizeof(float) * 3, nr_vx));
    CERR_RET_CERR(mesh_attr_dup(m, MESH_NORM, norm, sizeof(float) * 3, nr_vx));
    CERR_RET_CERR(mesh_attr_dup(m, MESH_TX,   tx,   sizeof(float) * 2, nr_vx));
    CERR_RET_CERR(mesh_attr_dup(m, MESH_IDX,  (void *)lines, sizeof(unsigned short),
                                nr_lines));
    mesh_aabb_calc(m);

    /*
     * The unlit debug program: the overlay is scene geometry, and under
     * the "model" shader the slab bands over the flights stamp their
     * shadows out of it - a dark rectangle across every staircase. The
     * lines must read the same everywhere.
     */
    struct shader_prog *prog = CRES_RET_CERR(
        pipeline_shader_find_get(clap_get_pipeline(scene->clap_ctx), "debug"));
    LOCAL_SET(model3d, model) = CRES_RET_CERR(
        ref_new_checked(model3d, .prog = ref_pass(prog), .mesh = m, .name = "navmesh"));
    model->draw_type = DRAW_TYPE_LINES;   /* the index buffer is edge pairs */
    model->cull_face = false;
    model->skip_shadow = true;

    LOCAL_SET(model3dtx, txm) = CRES_RET_CERR(
        ref_new_checked(model3dtx, .model = ref_pass(model), .tex = white_pixel()));
    scene_add_model(scene, txm);

    entity3d *e = CRES_RET_CERR(ref_new_checked(entity3d, .txmodel = ref_pass(txm)));
    entity3d_position(e, (vec3){ 0.0f, 0.0f, 0.0f });
    entity3d_color(e, COLOR_PT_ALL, (vec4){ 0.15f, 1.0f, 0.8f, 1.0f });
    entity3d_set_lod(e, 0, true);   /* the triangle LODs are meaningless for lines */
    entity3d_visible(e, 0);         /* off until the debug checkbox turns it on */
    e->outline_exclude = true;      /* a wireframe is all edge already */

    struct entity3d **slot = darray_add(nm->debug_entities);

    if (!slot)  return CERR_NOMEM;
    *slot = e;

    return CERR_OK;
}

/*
 * Build the hidden wireframe overlay of the walkable surface and stash it
 * on the navmesh for the debug UI to toggle. The render stack draws 16-bit
 * indices, and a building-sized mesh welds more corners than one index run
 * addresses, so the triangles are emitted in chunks, each closed off
 * before its vertex count would overflow an unsigned short; only the seam
 * triangles pay for their corners twice. Idempotent: built once and handed
 * to the scene's model queue, which owns the entities from then on (a
 * failure partway leaves the built chunks there - a partial overlay, not a
 * leak).
 */
cerr navmesh_debug_draw(navmesh *nm, struct scene *scene)
{
    if (!nm->baked || !nm->nr_tris) return CERR_INVALID_OPERATION;

    if (darray_count(nm->debug_entities))   return CERR_OK;

    /* global-to-chunk corner remap; the edge run sized for the worst chunk */
    LOCAL_SET(void, remap) = mem_alloc(sizeof(int), .nr = nm->nr_verts);
    LOCAL_SET(void, vx)    = mem_alloc(sizeof(float) * 3, .nr = NM_OVERLAY_VERTS);
    LOCAL_SET(void, lines) = mem_alloc(sizeof(unsigned short) * 6, .nr = nm->nr_tris);

    if (!remap || !vx || !lines)    return CERR_NOMEM;

    int *remapi = remap;
    float *vxf = vx;
    unsigned short *linesu = lines;
    size_t nr_vx = 0, nr_lines = 0;

    memset(remapi, 0xff, sizeof(int) * nm->nr_verts);

    for (size_t t = 0; t < nm->nr_tris; t++) {
        int fresh = 0;

        for (int k = 0; k < 3; k++) fresh += remapi[nm->tris[t].v[k]] < 0;

        if (nr_vx + fresh > NM_OVERLAY_VERTS) {
            CERR_RET_CERR(nm_overlay_emit(nm, scene, vxf, nr_vx, linesu, nr_lines));
            memset(remapi, 0xff, sizeof(int) * nm->nr_verts);
            nr_vx = nr_lines = 0;
            t--;    /* redo this triangle against the fresh chunk */
            continue;
        }

        unsigned short l[3];

        for (int k = 0; k < 3; k++) {
            int g = nm->tris[t].v[k];

            if (remapi[g] < 0) {
                remapi[g] = nr_vx;
                vxf[nr_vx * 3 + 0] = nm->verts[g][0];
                vxf[nr_vx * 3 + 1] = nm->verts[g][1] + NM_OVERLAY_LIFT;
                vxf[nr_vx * 3 + 2] = nm->verts[g][2];
                nr_vx++;
            }
            l[k] = remapi[g];
        }

        /* the triangle's edges as a line list */
        static const int e[6] = { 0, 1, 1, 2, 2, 0 };

        for (int k = 0; k < 6; k++) linesu[nr_lines++] = l[e[k]];
    }

    if (nr_lines)   CERR_RET_CERR(nm_overlay_emit(nm, scene, vxf, nr_vx, linesu, nr_lines));

    return CERR_OK;
}

bool navmesh_debug_shown(navmesh *nm)
{
    if (!darray_count(nm->debug_entities))  return false;

    return entity3d_matches(nm->debug_entities.x[0], ENTITY3D_VISIBLE);
}

void navmesh_debug_show(navmesh *nm, bool show)
{
    for (size_t i = 0; i < darray_count(nm->debug_entities); i++)   entity3d_visible(nm->debug_entities.x[i], show);
}

/*
 * Draw the roamer's remaining path as debug lines: yellow segments from the
 * agent through the waypoints, a red pole at the destination. The roamer core
 * stays clear of the scene and its message bus; this is the one place path
 * following reaches into them.
 */
void roamer_draw_path(roamer *r, struct scene *scene, const vec3 pos)
{
    for (int i = r->wp; i < r->nr_path; i++) {
        const float *a = i == r->wp ? pos : r->path[i - 1];
        const float *b = r->path[i];

        message_send(scene->clap_ctx, &(struct message) {
            .type       = MT_DEBUG_DRAW,
            .debug_draw = (struct message_debug_draw) {
                .color      = { 1.0, 0.85, 0.1, 1.0 },
                .thickness  = 2.0,
                .shape      = DEBUG_DRAW_LINE,
                .v0         = { a[0], a[1] + 0.15f, a[2] },
                .v1         = { b[0], b[1] + 0.15f, b[2] },
            }
        });
    }

    if (r->nr_path) {
        const float *dest = r->path[r->nr_path - 1];

        message_send(scene->clap_ctx, &(struct message) {
            .type       = MT_DEBUG_DRAW,
            .debug_draw = (struct message_debug_draw) {
                .color      = { 1.0, 0.25, 0.1, 1.0 },
                .thickness  = 3.0,
                .shape      = DEBUG_DRAW_LINE,
                .v0         = { dest[0], dest[1], dest[2] },
                .v1         = { dest[0], dest[1] + 1.6f, dest[2] },
            }
        });
    }
}

/* the pathing radius as a ring of debug lines around the agent's feet */
static void roamer_draw_radius(roamer *r, struct scene *scene)
{
    float radius = r->cfg.radius > 0.0f ? r->cfg.radius : r->nm->params.agent_radius;
    vec3 prev;

    for (int i = 0; i <= 24; i++) {
        float ang = 2.0f * (float)M_PI * i / 24.0f;
        vec3 p = {
            r->dbg_pos[0] + radius * cosf(ang),
            r->ground + 0.15f,
            r->dbg_pos[2] + radius * sinf(ang),
        };

        if (i)
            message_send(scene->clap_ctx, &(struct message) {
                .type       = MT_DEBUG_DRAW,
                .debug_draw = (struct message_debug_draw) {
                    .color      = { 0.2, 0.9, 1.0, 1.0 },
                    .thickness  = 2.0,
                    .shape      = DEBUG_DRAW_LINE,
                    .v0         = { prev[0], prev[1], prev[2] },
                    .v1         = { p[0], p[1], p[2] },
                }
            });
        vec3_dup(prev, p);
    }
}

void navmesh_roamers_debug(navmesh *nm, struct scene *scene)
{
    roamer *r;

    /*
     * The overlays draw independently of the window: flip a toggle,
     * close the window, watch the agent walk.
     */
    list_for_each_entry(r, &nm->roamers, entry) {
        if (nm->all_paths || r->show_path)  roamer_draw_path(r, scene, r->dbg_pos);
        if (r->show_radius)                 roamer_draw_radius(r, scene);
    }

    debug_module *dbgm = ui_igBegin(DEBUG_ROAMERS, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display) return;

    if (dbgm->unfolded) {
        if (ui_igControlTableHeader("roamers", "show all paths")) {
            ui_igCheckbox("show all paths", &nm->all_paths);
            igEndTable();
        }

        int i = 0;

        list_for_each_entry(r, &nm->roamers, entry) {
            /* names are the callers' business; the index keeps ids unique */
            if (!ui_igControlTableHeader("%s##%d", "slow dist",
                                         r->name ? r->name : "roamer", ++i))
                continue;

            if (r->nr_path)
                ui_igTableRow("state", "wp %d/%d -> (%.0f, %.0f, %.0f)",
                              r->wp, r->nr_path,
                              r->path[r->nr_path - 1][0], r->path[r->nr_path - 1][1],
                              r->path[r->nr_path - 1][2]);
            else
                ui_igTableRow("state", "idle");
            ui_igTableRow("pos", "%.1f %.1f %.1f",
                          r->dbg_pos[0], r->dbg_pos[1], r->dbg_pos[2]);
            ui_igTableRow("speed", "%.2f m/s, yaw %.0f deg",
                          r->speed, to_degrees(r->yaw));
            ui_igTableRow("stuck", "%.2f s%s", r->stuck,
                          r->repathed ? " (re-pathed)" : "");
            ui_igTableRow("tri", "%d, ground %.2f", r->tri, r->ground);

            ui_igCheckbox("auto roam", &r->cfg.auto_roam);
            ui_igCheckbox("show path", &r->show_path);
            ui_igCheckbox("show radius", &r->show_radius);

            ui_igSliderFloat("radius", &r->cfg.radius, 0.0f, 4.0f, "%.2f",
                             ImGuiSliderFlags_AlwaysClamp);
            ui_igSliderFloat("min dist", &r->cfg.min_dist, 1.0f, 60.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp);
            ui_igSliderFloat("arrive", &r->cfg.arrive, 0.05f, 2.0f, "%.2f",
                             ImGuiSliderFlags_AlwaysClamp);
            ui_igSliderFloat("turn rate", &r->cfg.turn_rate, 0.2f, 8.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp);
            ui_igSliderFloat("accel", &r->cfg.accel, 0.2f, 8.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp);
            ui_igSliderFloat("run dist", &r->cfg.run_dist, 0.0f, 30.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp);
            ui_igSliderFloat("slow dist", &r->cfg.slow_dist, 0.0f, 6.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp);

            igEndTable();
        }
    }

    ui_igEnd(DEBUG_ROAMERS);
}
