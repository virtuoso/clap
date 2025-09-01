// SPDX-License-Identifier: Apache-2.0
#include "linmath.h"
#include "mesh.h"
#include "model.h"
#include "primitives.h"

void _prim_calc_normals(size_t vx_idx, const prim_emit_opts *opts)
{
    if (vx_idx % 3) return;

    vec3 triangle[3];
    for (int i = 0; i < 3; i++)
        vec3_dup(triangle[i], &mesh_vx(opts->mesh)[(vx_idx + i) * 3]);

    vec3 a, b, norm;
    vec3_sub(a, triangle[0], triangle[1]);
    vec3_sub(b, triangle[0], triangle[2]);
    vec3_mul_cross(norm, a, b);
    vec3_norm(norm, norm);

    float *mesh_norm_array = mesh_norm(opts->mesh);
    for (int i = 0; i < 3; i++) {
        mesh_norm_array[(vx_idx + i) * 3 + 0] = norm[0];
        mesh_norm_array[(vx_idx + i) * 3 + 1] = norm[1];
        mesh_norm_array[(vx_idx + i) * 3 + 2] = norm[2];
    }
}

void _prim_emit_vertex(vec3 pos, const prim_emit_opts *opts)
{
    /*
     * Must have vx and idx;
     * Only updating normals on eveny 3rd vertex -- need 3 vertices to
     * calculate them
     */
    if (!mesh_vx(opts->mesh) || !mesh_idx(opts->mesh))
        return;

    size_t vx_idx = mesh_nr_vx(opts->mesh);
    mesh_vx(opts->mesh)[vx_idx * 3 + 0] = pos[0];
    mesh_vx(opts->mesh)[vx_idx * 3 + 1] = pos[1];
    mesh_vx(opts->mesh)[vx_idx * 3 + 2] = pos[2];

    unsigned short idx_idx = mesh_nr_idx(opts->mesh);
    mesh_idx(opts->mesh)[idx_idx++] = vx_idx;

    if (mesh_tx(opts->mesh)) {
        mesh_tx(opts->mesh)[vx_idx * 2 + 0] = opts->uv ? opts->uv[0] : 0.0;
        mesh_tx(opts->mesh)[vx_idx * 2 + 1] = opts->uv ? opts->uv[1] : 0.0;
    }

    vx_idx++;
    mesh_attr(opts->mesh, MESH_VX)->nr = vx_idx;
    mesh_attr(opts->mesh, MESH_TX)->nr = vx_idx;
    mesh_attr(opts->mesh, MESH_IDX)->nr = idx_idx;

    size_t norm_idx = mesh_nr_norm(opts->mesh);
    if (mesh_norm(opts->mesh) && !(vx_idx % 3)) {
        /* 3 new vertices added, normals should be 3 behind */
        err_on(norm_idx != vx_idx - 3, "norm_idx != vx_idx - 3: %zu, %zu\n", norm_idx, vx_idx);

        _prim_calc_normals(norm_idx, opts);

        mesh_attr(opts->mesh, MESH_NORM)->nr = norm_idx + 3;
    }
}

void _prim_emit_triangle(vec3 triangle[3], const prim_emit_opts *opts)
{
    _prim_emit_vertex(triangle[0], opts);
    if (opts->clockwise) {
        _prim_emit_vertex(triangle[2], opts);
        _prim_emit_vertex(triangle[1], opts);
    } else {
        _prim_emit_vertex(triangle[1], opts);
        _prim_emit_vertex(triangle[2], opts);
    }
}

void _prim_emit_triangle3(vec3 v0, vec3 v1, vec3 v2, const prim_emit_opts *opts)
{
    _prim_emit_vertex(v0, opts);
    if (opts->clockwise) {
        _prim_emit_vertex(v2, opts);
        _prim_emit_vertex(v1, opts);
    } else {
        _prim_emit_vertex(v1, opts);
        _prim_emit_vertex(v2, opts);
    }
}

void _prim_emit_quad(vec3 quad[4], const prim_emit_opts *opts)
{
    _prim_emit_triangle3(quad[0], quad[3], quad[1], opts);
    _prim_emit_triangle3(quad[3], quad[2], quad[1], opts);
}

void _prim_emit_cylinder(vec3 org, float height, float radius, int nr_serments, const prim_emit_opts *opts)
{
    struct mesh *mesh = opts->mesh;
    if (!mesh)  return;

    /*
     * a triangle per each segment at the top and bottom, plus 2 triangles (quad)
     * for each side: 4 triangles (12 vertices) per segment
     */
    int nr_vert = nr_serments * 12;
    if (!mesh->attr[MESH_VX].data) {
        CERR_RET(mesh_attr_alloc(mesh, MESH_VX, sizeof(float) * 3, nr_vert), return);
        CERR_RET(mesh_attr_alloc(mesh, MESH_TX, sizeof(float) * 2, nr_vert), return);
        CERR_RET(mesh_attr_alloc(mesh, MESH_NORM, sizeof(float) * 3, nr_vert), return);
        CERR_RET(mesh_attr_alloc(mesh, MESH_IDX, sizeof(unsigned short), nr_vert), return);
    } else {
        nr_vert += mesh_nr_vx(mesh);
        CERR_RET(mesh_attr_resize(mesh, MESH_VX, nr_vert), return);
        CERR_RET(mesh_attr_resize(mesh, MESH_TX, nr_vert), return);
        CERR_RET(mesh_attr_resize(mesh, MESH_NORM, nr_vert), return);
        CERR_RET(mesh_attr_resize(mesh, MESH_IDX, nr_vert), return);
    }

    int seg;
    for (seg = 0; seg < nr_serments; seg++) {
        err_on(mesh_nr_vx(mesh) >= nr_vert,
               "last_vert: %zu nr_vert: %d\n", mesh_nr_vx(mesh), nr_vert);
        vec3 triangle[3];

        vec2 seg_vert1, seg_vert2;
        seg_vert1[0] = org[0] + radius * cos(M_PI * 2 * seg / nr_serments);
        seg_vert1[1] = org[2] + radius * sin(M_PI * 2 * seg / nr_serments);
        seg_vert2[0] = org[0] + radius * cos(M_PI * 2 * (seg + 1) / nr_serments);
        seg_vert2[1] = org[2] + radius * sin(M_PI * 2 * (seg + 1) / nr_serments);

        /* bottom */
        vec3_dup(triangle[0], org);
        triangle[1][0] = seg_vert1[0];
        triangle[1][1] = org[1];
        triangle[1][2] = seg_vert1[1];
        triangle[2][0] = seg_vert2[0];
        triangle[2][1] = org[1];
        triangle[2][2] = seg_vert2[1];
        _prim_emit_triangle(triangle, opts);

        /* side quad */
        vec3 quad[4];
        quad[0][0] = seg_vert1[0];
        quad[0][1] = org[1];
        quad[0][2] = seg_vert1[1];
        quad[1][0] = seg_vert2[0];
        quad[1][1] = org[1];
        quad[1][2] = seg_vert2[1];
        quad[2][0] = seg_vert2[0];
        quad[2][1] = org[1] + height;
        quad[2][2] = seg_vert2[1];
        quad[3][0] = seg_vert1[0];
        quad[3][1] = org[1] + height;
        quad[3][2] = seg_vert1[1];
        _prim_emit_quad(quad, opts);

        /* top */
        vec3_dup(triangle[0], org);
        triangle[0][1] = org[1] + height;
        triangle[1][0] = seg_vert2[0];
        triangle[1][1] = org[1] + height;
        triangle[1][2] = seg_vert2[1];
        triangle[2][0] = seg_vert1[0];
        triangle[2][1] = org[1] + height;
        triangle[2][2] = seg_vert1[1];
        _prim_emit_triangle(triangle, opts);
    }
}

cresp(model3d) model3d_new_cylinder(struct shader_prog *p, vec3 org, float height, float radius, int nr_serments)
{
    LOCAL_SET(mesh_t, cylinder_mesh) = CRES_RET_T(ref_new_checked(mesh, .name = "cylinder"), model3d);

    _prim_emit_cylinder(org, height, radius, nr_serments, &(const prim_emit_opts) { .mesh = cylinder_mesh });

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

model3d *model3d_new_quad(struct shader_prog *p, float x, float y, float z, float w, float h)
{
    static unsigned short quad_idx[] = {0, 3, 1, 3, 2, 1};
    float quad_vx[] = {
        x, y + h, z, x, y, z, x + w, y, z, x + w, y + h, z,
    };
    struct mesh quad_mesh = {
        .ref    = REF_STATIC(mesh),
        .name   = "quad",
        .attr   = {
            [MESH_VX] = { .data = quad_vx, .nr = array_size(quad_vx) / 3, .stride = sizeof(float) * 3 },
            [MESH_TX] = { .data = quad_tx, .nr = array_size(quad_tx) / 2, .stride = sizeof(float) * 2 },
            [MESH_IDX] = { .data = quad_idx, .nr = array_size(quad_idx), .stride = sizeof(unsigned short) },
        }
    };

    return ref_new(model3d,
                   .name        = "quad",
                   .prog        = p,
                   .mesh        = &quad_mesh,
                   .skip_aabb   = true);
}

model3d *model3d_new_quadrev(struct shader_prog *p, float x, float y, float z, float w, float h)
{
    static unsigned short quad_idx[] = {0, 1, 3, 3, 1, 2};
    float quad_vx[] = {
        x, y + h, z, x, y, z, x + w, y, z, x + w, y + h, z,
    };
    struct mesh quad_mesh = {
        .ref    = REF_STATIC(mesh),
        .name   = "quad",
        .attr   = {
            [MESH_VX] = { .data = quad_vx, .nr = array_size(quad_vx) / 3, .stride = sizeof(float) * 3 },
            [MESH_TX] = { .data = quad_tx, .nr = array_size(quad_tx) / 2, .stride = sizeof(float) * 2 },
            [MESH_IDX] = { .data = quad_idx, .nr = array_size(quad_idx), .stride = sizeof(unsigned short) },
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
