// SPDX-License-Identifier: Apache-2.0
#include "mesh.h"
#include "model.h"
#include "primitives.h"
#include "util.h"

cresp(model3d) _prim_end_model3d(struct shader_prog *p, const prim_emit_opts *opts)
{
    auto mesh = opts->mesh;

    if (!mesh)  return cresp_error(model3d, CERR_INVALID_ARGUMENTS);

    const char *name = opts->name;
    if (!name)  name = mesh->name;
    if (!name)  return cresp_error(model3d, CERR_INVALID_ARGUMENTS);

    mesh_aabb_calc(mesh);
    mesh_optimize(mesh);

    return ref_new_checked(
        model3d,
        .name = mesh->name,
        .prog = p,
        .mesh = mesh
    );
}

cresp(model3dtx) _prim_end_model3dtx(struct shader_prog *p, texture_t *tex, struct mq *mq, const prim_emit_opts *opts)
{
    LOCAL_SET(model3d, m) = CRES_RET_T(_prim_end_model3d(p, opts), model3dtx);

    auto txm = CRES_RET_T(ref_new_checked(model3dtx, .model = ref_pass(m), .tex = tex), model3dtx);
    mq_add_model(mq, txm);

    return cresp_val(model3dtx, txm);
}

cresp(model3d) model3d_new_cylinder(struct shader_prog *p, vec3 org, float height, float radius, int nr_serments)
{
    LOCAL_SET(mesh_t, cylinder_mesh) = CRES_RET_T(ref_new_checked(mesh, .name = "cylinder"), model3d);

    CERR_RET_T(_prim_emit_cylinder(org, height, radius, nr_serments,
                                   &(const prim_emit_opts) { .mesh = cylinder_mesh }),
               model3d);

    mesh_aabb_calc(cylinder_mesh);
    mesh_optimize(cylinder_mesh);

    return ref_new_checked(
        model3d,
        .name = "cylinder",
        .prog = p,
        .mesh = cylinder_mesh
    );
}

/*
 * The following don't use prim_emit_*(), because static arrays are faster, which
 * is a bonus, because quads, for example, are used in UI, at render time.
 */
static float quad_tx[]  = {
    0.0, 0.0,
    0.0, 1.0,
    1.0, 1.0,
    1.0, 0.0,
};

static float cube_vx[] = {
    // back
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f,
    // front
    0.0f, 1.0f, 1.0f,
    0.0f, 0.0f, 1.0f,
    1.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 1.0f,
    // left
    1.0f, 1.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 1.0f,
    // right
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
    0.0f, 1.0f, 1.0f,
    // top
    0.0f, 1.0f, 1.0f,
    0.0f, 1.0f, 0.0f,
    1.0f, 1.0f, 0.0f,
    1.0f, 1.0f, 1.0f,
    // bottom
    0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 1.0f
};

static float cube_norm[] = {
    // back
    0, 0, -1,
    0, 0, -1,
    0, 0, -1,
    0, 0, -1,
    // front
    0, 0, 1,
    0, 0, 1,
    0, 0, 1,
    0, 0, 1,
    // left
    -1, 0, 0,
    -1, 0, 0,
    -1, 0, 0,
    -1, 0, 0,
    // right
    1, 0, 0,
    1, 0, 0,
    1, 0, 0,
    1, 0, 0,
    // top
    0, 1, 0,
    0, 1, 0,
    0, 1, 0,
    0, 1, 0,
    // bottom
    0, -1, 0,
    0, -1, 0,
    0, -1, 0,
    0, -1, 0
};

static unsigned short cube_idx[] = {
    0, 3, 1,
    1, 3, 2,
    4, 5, 7,
    7, 5, 6,
    8, 11, 9,
    9, 11, 10,
    12, 13, 15,
    15, 13, 14,
    16, 19, 17,
    19, 18, 17,
    20, 21, 23,
    23, 21, 22
};

static float cube_tx[] = {
    1.0f, 1.0f, /* Back. */
    0.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 0.0f,
    0.0f, 1.0f, /* Front. */
    1.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 0.0f,
    0.0f, 1.0f, /* Left. */
    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    1.0f, 1.0f, /* Right. */
    1.0f, 0.0f,
    0.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 1.0f, /* Top. */
    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 0.0f, /* Bottom. */
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f
};

extern struct ref_class ref_class_mesh;

/* For mesh_push_mesh() */
struct mesh cube_mesh = {
    .ref    = REF_STATIC(mesh),
    .name   = "cube",
    .attr   = {
        [MESH_VX] = { .data = cube_vx, .nr = array_size(cube_vx) / 3, .stride = sizeof(float) * 3 },
        [MESH_TX] = { .data = cube_tx, .nr = array_size(cube_tx) / 2, .stride = sizeof(float) * 2 },
        [MESH_NORM] = { .data = cube_norm, .nr = array_size(cube_norm) / 3, .stride = sizeof(float) * 3 },
        [MESH_IDX] = { .data = cube_idx, .nr = array_size(cube_idx), .stride = sizeof(unsigned short) },
    }
};

model3d *model3d_new_cube(struct shader_prog *p, bool skip_aabb)
{
    return ref_new(model3d,
                   .name        = "cube",
                   .prog        = p,
                   .mesh        = &cube_mesh,
                   .skip_aabb   = skip_aabb);
}

/**
 * model3d_new_quad() - create a front-facing (CCW) textured quad
 * @p:          shader program
 * @x, @y:     origin corner position
 * @z:         depth
 * @w, @h:     size (may be negative to flip along that axis)
 *
 * Vertices span from (x, y) to (x+w, y+h); UVs map (0,0)..(1,1)
 * accordingly. Winding is always CCW (front-facing) regardless of
 * the signs of w and h.
 */
model3d *model3d_new_quad(struct shader_prog *p, float x, float y, float z, float w, float h)
{
    /* CCW when w*h > 0; flip to CW (which reads as CCW after the axis flip) when w*h < 0 */
    static unsigned short ccw_idx[] = {0, 1, 3, 3, 1, 2};
    static unsigned short cw_idx[]  = {0, 3, 1, 3, 2, 1};
    unsigned short *idx = (w * h >= 0) ? ccw_idx : cw_idx;

    float quad_vx[] = {
        x, y + h, z, x, y, z, x + w, y, z, x + w, y + h, z,
    };
    struct mesh quad_mesh = {
        .ref    = REF_STATIC(mesh),
        .name   = "quad",
        .attr   = {
            [MESH_VX] = { .data = quad_vx, .nr = array_size(quad_vx) / 3, .stride = sizeof(float) * 3 },
            [MESH_TX] = { .data = quad_tx, .nr = array_size(quad_tx) / 2, .stride = sizeof(float) * 2 },
            [MESH_IDX] = { .data = idx, .nr = 6, .stride = sizeof(unsigned short) },
        }
    };

    return ref_new(model3d,
                   .name        = "quad",
                   .prog        = p,
                   .mesh        = &quad_mesh,
                   .skip_aabb   = true);
}

static unsigned short frame_idx[] = {
    4, 0, 5, 0, 1, 5,
    5, 1, 2, 5, 2, 6,
    6, 2, 3, 6, 3, 7,
    7, 3, 0, 7, 0, 4, };

static float frame_tx[]  = {
    0.0, 0.0,
    0.0, 1.0,
    1.0, 1.0,
    1.0, 0.0,
    0.5, 0.5,
    0.5, 0.5,
    0.5, 0.5,
    0.5, 0.5,
};

model3d *model3d_new_frame(struct shader_prog *p, float x, float y, float z, float w, float h, float t)
{
    float frame_vx[] = {
        x, y + h, z, x, y, z, x + w, y, z, x + w, y + h, z,
        x + t, y + h - t, z, x + t, y + t, z, x + w - t, y + t, z, x + w - t, y + h - t, z,
    };
    struct mesh frame_mesh = {
        .ref    = REF_STATIC(mesh),
        .name   = "quad",
        .attr   = {
            [MESH_VX] = { .data = frame_vx, .nr = array_size(frame_vx) / 3, .stride = sizeof(float) * 3 },
            [MESH_TX] = { .data = frame_tx, .nr = array_size(frame_tx) / 2, .stride = sizeof(float) * 2 },
            [MESH_IDX] = { .data = frame_idx, .nr = array_size(frame_idx), .stride = sizeof(unsigned short) },
        }
    };

    return ref_new(model3d,
        .name       = "frame",
        .prog       = p,
        .mesh       = &frame_mesh,
        .skip_aabb  = true);
}
