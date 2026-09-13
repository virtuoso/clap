// SPDX-License-Identifier: Apache-2.0
/*
 * Navmesh debug visualization: a wireframe overlay of the walkable surface.
 *
 * This reaches into the scene/render stack, which the rest of the navmesh
 * deliberately does not. It lives in its own compilation unit, built only
 * for non-final engine configurations (gated in core/nav/CMakeLists.txt, which
 * also defines CLAP_NAVMESH_DEBUG); final and test builds link neither this
 * file nor the render dependencies it pulls in.
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
#include "navmesh.h"
#include "navmesh-internal.h"
#include "nav/roamer.h"

/*
 * Build a hidden wireframe overlay of the walkable surface and stash it on
 * the navmesh for the debug UI to toggle. Idempotent: built once and handed
 * to the scene's model queue, which owns it from then on. The scratch
 * buffers are LOCAL()s, mem_free()'d at scope exit so every error path here
 * carries no cleanup.
 */
cresp(entity3d) navmesh_debug_draw(navmesh *nm, struct scene *scene)
{
    if (nm->debug_entity)
        return cresp_val(entity3d, nm->debug_entity);

    LOCAL(void, vx);
    LOCAL(void, idx);
    size_t nr_vx = 0, nr_idx = 0;

    CERR_RET_T(navmesh_geometry(nm, 0.12f, (float **)&vx, &nr_vx,
                                (unsigned short **)&idx, &nr_idx), entity3d);

    /* flat-up normals, zero UVs, and the triangle edges as a line list */
    LOCAL_SET(void, norm)  = mem_alloc(sizeof(float) * 3, .nr = nr_vx);
    LOCAL_SET(void, tx)    = mem_alloc(sizeof(float) * 2, .nr = nr_vx, .zero = 1);
    LOCAL_SET(void, lines) = mem_alloc(sizeof(unsigned short) * 2, .nr = nr_idx);
    if (!norm || !tx || !lines)
        return cresp_error(entity3d, CERR_NOMEM);

    float *normf = norm;
    for (size_t i = 0; i < nr_vx; i++)  normf[i * 3 + 1] = 1.0f;

    unsigned short *linesu = lines, *idxu = idx;
    size_t nr_lines = 0;
    for (size_t i = 0; i + 2 < nr_idx; i += 3) {
        static const int e[6] = { 0, 1, 1, 2, 2, 0 };

        for (int k = 0; k < 6; k++)
            linesu[nr_lines++] = idxu[i + e[k]];
    }

    LOCAL_SET(mesh_t, m) = CRES_RET_T(ref_new_checked(mesh, .name = "navmesh"), entity3d);
    CERR_RET_T(mesh_attr_dup(m, MESH_VX,   vx,   sizeof(float) * 3, nr_vx), entity3d);
    CERR_RET_T(mesh_attr_dup(m, MESH_NORM, norm, sizeof(float) * 3, nr_vx), entity3d);
    CERR_RET_T(mesh_attr_dup(m, MESH_TX,   tx,   sizeof(float) * 2, nr_vx), entity3d);
    CERR_RET_T(mesh_attr_dup(m, MESH_IDX,  lines, sizeof(unsigned short), nr_lines), entity3d);
    mesh_aabb_calc(m);

    struct shader_prog *prog = CRES_RET_T(
        pipeline_shader_find_get(clap_get_pipeline(scene->clap_ctx), "model"), entity3d);
    LOCAL_SET(model3d, model) = CRES_RET_T(
        ref_new_checked(model3d, .prog = ref_pass(prog), .mesh = m, .name = "navmesh"), entity3d);
    model->draw_type = DRAW_TYPE_LINES;   /* the index buffer is edge pairs */
    model->cull_face = false;
    model->skip_shadow = true;

    LOCAL_SET(model3dtx, txm) = CRES_RET_T(
        ref_new_checked(model3dtx, .model = ref_pass(model), .tex = white_pixel()), entity3d);
    scene_add_model(scene, txm);

    entity3d *e = CRES_RET_T(ref_new_checked(entity3d, .txmodel = ref_pass(txm)), entity3d);
    entity3d_position(e, (vec3){ 0.0f, 0.0f, 0.0f });
    entity3d_color(e, COLOR_PT_ALL, (vec4){ 0.15f, 1.0f, 0.8f, 1.0f });
    entity3d_set_lod(e, 0, true);   /* the triangle LODs are meaningless for lines */
    entity3d_visible(e, 0);         /* off until the debug checkbox turns it on */
    nm->debug_entity = e;

    return cresp_val(entity3d, e);
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
