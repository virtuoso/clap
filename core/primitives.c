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
        mesh_tx(opts->mesh)[vx_idx * 2 + 0] = opts->uv ? opts->uv[0][0] : 0.0;
        mesh_tx(opts->mesh)[vx_idx * 2 + 1] = opts->uv ? opts->uv[0][1] : 0.0;
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
    prim_emit_opts _opts = *opts;
    vec2 uv[] = { { 0.0, 0.0 }, { 0.0, 1.0 }, { 1.0, 0.0 } };
    if (!_opts.uv)  _opts.uv = uv;

    _opts.uv = opts->uv ? &opts->uv[0] : &uv[0];
    _prim_emit_vertex(triangle[0], &_opts);
    if (_opts.clockwise) {
        _opts.uv = opts->uv ? &opts->uv[2] : &uv[2];
        _prim_emit_vertex(triangle[2], &_opts);
        _opts.uv = opts->uv ? &opts->uv[1] : &uv[1];
        _prim_emit_vertex(triangle[1], &_opts);
    } else {
        _opts.uv = opts->uv ? &opts->uv[1] : &uv[1];
        _prim_emit_vertex(triangle[1], &_opts);
        _opts.uv = opts->uv ? &opts->uv[2] : &uv[2];
        _prim_emit_vertex(triangle[2], &_opts);
    }
}

void _prim_emit_triangle3(vec3 v0, vec3 v1, vec3 v2, const prim_emit_opts *opts)
{
    prim_emit_opts _opts = *opts;
    vec2 uv[] = { { 0.0, 0.0 }, { 0.0, 1.0 }, { 1.0, 0.0 } };
    if (!_opts.uv)  _opts.uv = uv;

    _opts.uv = opts->uv ? &opts->uv[0] : &uv[0];
    _prim_emit_vertex(v0, &_opts);
    if (_opts.clockwise) {
        _opts.uv = opts->uv ? &opts->uv[2] : &uv[2];
        _prim_emit_vertex(v2, &_opts);
        _opts.uv = opts->uv ? &opts->uv[1] : &uv[1];
        _prim_emit_vertex(v1, &_opts);
    } else {
        _opts.uv = opts->uv ? &opts->uv[1] : &uv[1];
        _prim_emit_vertex(v1, &_opts);
        _opts.uv = opts->uv ? &opts->uv[2] : &uv[2];
        _prim_emit_vertex(v2, &_opts);
    }
}

void _prim_emit_quad(vec3 quad[4], const prim_emit_opts *opts)
{
    prim_emit_opts _opts = *opts;
    vec2 uv[] = { { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 }, { 0.0, 1.0 } };
    bool uv_default = !_opts.uv;

    vec2 tri0_uv[3], tri1_uv[3];

    if (uv_default) {
        vec2_dup(tri0_uv[0], uv[0]); vec2_dup(tri0_uv[1], uv[3]); vec2_dup(tri0_uv[2], uv[1]);
        _opts.uv = tri0_uv;
    }

    _prim_emit_triangle3(quad[0], quad[3], quad[1], &_opts);

    if (uv_default) {
        vec2_dup(tri1_uv[0], uv[3]); vec2_dup(tri1_uv[1], uv[2]); vec2_dup(tri1_uv[2], uv[1]);
        _opts.uv = tri1_uv;
    }

    _prim_emit_triangle3(quad[3], quad[2], quad[1], &_opts);
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
        if (!(opts->skip_mask & (1ull << 0))) {
            vec3_dup(triangle[0], org);
            triangle[1][0] = seg_vert1[0];
            triangle[1][1] = org[1];
            triangle[1][2] = seg_vert1[1];
            triangle[2][0] = seg_vert2[0];
            triangle[2][1] = org[1];
            triangle[2][2] = seg_vert2[1];
            _prim_emit_triangle(triangle, opts);
        }

        /* side quad */
        if (!(opts->skip_mask & (1ull << (seg + 2)))) {
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
        }

        /* top */
        if (!(opts->skip_mask & (1ull << 1))) {
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
}

static inline void _prim_sphere_vx(vec3 out, vec3 org, float radius, float theta, float phi)
{
    float sin_theta = sin(theta);

    out[0] = org[0] + radius * sin_theta * cos(phi);
    out[1] = org[1] + radius * cos(theta);
    out[2] = org[2] + radius * sin_theta * sin(phi);
}

static inline void _prim_sphere_uv(vec2 out, float theta, float phi)
{
    out[0] = phi / (M_PI * 2);
    out[1] = theta / M_PI;
}

static inline void _prim_emit_triangle3_uv(vec3 v0, vec3 v1, vec3 v2,
                                           vec2 uv0, vec2 uv1, vec2 uv2,
                                           const prim_emit_opts *opts)
{
    if (opts->uv) {
        _prim_emit_triangle3(v0, v1, v2, opts);
        return;
    }

    prim_emit_opts _opts = *opts;
    vec2 uv[3];

    vec2_dup(uv[0], uv0);
    vec2_dup(uv[1], uv1);
    vec2_dup(uv[2], uv2);
    _opts.uv = uv;

    _prim_emit_triangle3(v0, v1, v2, &_opts);
}

static inline void _prim_sphere_smooth_normals(vec3 org, const prim_emit_opts *opts)
{
    struct mesh *mesh = opts->mesh;
    if (!mesh || !mesh_norm(mesh) || mesh_nr_vx(mesh) < 3)
        return;

    size_t base_vx = mesh_nr_vx(mesh) - 3;
    for (size_t i = 0; i < 3; i++) {
        size_t vx_idx = base_vx + i;
        vec3 p, n;

        p[0] = mesh_vx(mesh)[vx_idx * 3 + 0];
        p[1] = mesh_vx(mesh)[vx_idx * 3 + 1];
        p[2] = mesh_vx(mesh)[vx_idx * 3 + 2];

        vec3_sub(n, p, org);
        vec3_norm(n, n);

        mesh_norm(mesh)[vx_idx * 3 + 0] = n[0];
        mesh_norm(mesh)[vx_idx * 3 + 1] = n[1];
        mesh_norm(mesh)[vx_idx * 3 + 2] = n[2];
    }
}

void _prim_emit_sphere(vec3 org, float radius, int nr_serments, const prim_emit_opts *opts)
{
    struct mesh *mesh = opts->mesh;
    if (!mesh)  return;

    if (nr_serments < 3)
        nr_serments = 3;

    int nr_stacks = nr_serments / 2;
    if (nr_stacks < 2)
        nr_stacks = 2;

    int nr_vert = nr_serments * 6 * (nr_stacks - 1);
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

    for (int stack = 0; stack < nr_stacks; stack++) {
        float theta0 = M_PI * stack / nr_stacks;
        float theta1 = M_PI * (stack + 1) / nr_stacks;

        for (int seg = 0; seg < nr_serments; seg++) {
            err_on(mesh_nr_vx(mesh) >= nr_vert,
                   "last_vert: %zu nr_vert: %d\n", mesh_nr_vx(mesh), nr_vert);

            float phi0 = M_PI * 2 * seg / nr_serments;
            float phi1 = M_PI * 2 * (seg + 1) / nr_serments;
            vec3 v0, v1, v2, v3;
            vec2 uv0, uv1, uv2, uv3;

            _prim_sphere_vx(v0, org, radius, theta0, phi0);
            _prim_sphere_vx(v1, org, radius, theta1, phi0);
            _prim_sphere_vx(v2, org, radius, theta1, phi1);
            _prim_sphere_vx(v3, org, radius, theta0, phi1);

            _prim_sphere_uv(uv0, theta0, phi0);
            _prim_sphere_uv(uv1, theta1, phi0);
            _prim_sphere_uv(uv2, theta1, phi1);
            _prim_sphere_uv(uv3, theta0, phi1);

            if (!stack) {
                _prim_emit_triangle3_uv(v0, v2, v1, uv0, uv2, uv1, opts);
                _prim_sphere_smooth_normals(org, opts);
            } else if (stack == nr_stacks - 1) {
                _prim_emit_triangle3_uv(v0, v3, v1, uv0, uv3, uv1, opts);
                _prim_sphere_smooth_normals(org, opts);
            } else {
                _prim_emit_triangle3_uv(v0, v2, v1, uv0, uv2, uv1, opts);
                _prim_sphere_smooth_normals(org, opts);
                _prim_emit_triangle3_uv(v0, v3, v2, uv0, uv3, uv2, opts);
                _prim_sphere_smooth_normals(org, opts);
            }
        }
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
