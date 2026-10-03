// SPDX-License-Identifier: Apache-2.0
#include <float.h>
#include <limits.h>
#include <math.h>
#include "linmath.h"
#include "memory.h"
#include "mesh.h"
#include "model.h"
#include "primitives.h"
#include "util.h"

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

void _prim_calc_tangents(size_t vx_idx, const prim_emit_opts *opts)
{
    if (vx_idx % 3) return;

    float *vx = &mesh_vx(opts->mesh)[vx_idx * 3];
    float *tx = &mesh_tx(opts->mesh)[vx_idx * 2];

    vec3 norm, e1, e2;
    vec3_sub(e1, &vx[3], &vx[0]);
    vec3_sub(e2, &vx[6], &vx[0]);
    vec3_mul_cross(norm, e1, e2);   /* the same face normal _prim_calc_normals() gets */
    vec3_norm(norm, norm);

    /*
     * flat, unshared face: solve + orthonormalize directly, no accumulation;
     * a failed solve leaves zeros and falls through to the perpendicular
     */
    vec3 tdir = { 0.0f, 0.0f, 0.0f }, bdir = { 0.0f, 0.0f, 0.0f };
    vec4 tangent;
    triangle_tangent(&vx[0], &vx[3], &vx[6], &tx[0], &tx[2], &tx[4], tdir, bdir);
    tangent_orthonormalize(norm, tdir, bdir, tangent);

    float *mesh_tan_array = mesh_tangent(opts->mesh);
    for (int i = 0; i < 3; i++) {
        mesh_tan_array[(vx_idx + i) * 4 + 0] = tangent[0];
        mesh_tan_array[(vx_idx + i) * 4 + 1] = tangent[1];
        mesh_tan_array[(vx_idx + i) * 4 + 2] = tangent[2];
        mesh_tan_array[(vx_idx + i) * 4 + 3] = tangent[3];
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

    if (mesh_tangent(opts->mesh) && !(vx_idx % 3)) {
        _prim_calc_tangents(vx_idx - 3, opts);

        mesh_attr(opts->mesh, MESH_TANGENTS)->nr = vx_idx;
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

/*
 * Indices are 16-bit (_prim_emit_vertex() emits one per vertex): @nr_vert
 * more vertices fit only if the total stays within USHRT_MAX
 */
static bool prim_fits(struct mesh *mesh, size_t nr_vert)
{
    size_t nr_vx = mesh ? mesh_nr_vx(mesh) : 0;

    return nr_vx <= USHRT_MAX && nr_vert <= USHRT_MAX - nr_vx;
}

cresp(mesh) _prim_begin(size_t nr_vert, const prim_emit_opts *opts)
{
    auto mesh = opts->mesh;

    if (!prim_fits(mesh, nr_vert))  return cresp_error(mesh, CERR_TOO_LARGE);

    if (!mesh)
        mesh = CRES_RET_T(ref_new_checked(mesh, .name = opts->name), mesh);

    if (!mesh->attr[MESH_VX].data) {
        CERR_RET_T(mesh_attr_alloc(mesh, MESH_VX, sizeof(float) * 3, nr_vert), mesh);
        CERR_RET_T(mesh_attr_alloc(mesh, MESH_TX, sizeof(float) * 2, nr_vert), mesh);
        CERR_RET_T(mesh_attr_alloc(mesh, MESH_NORM, sizeof(float) * 3, nr_vert), mesh);
        CERR_RET_T(mesh_attr_alloc(mesh, MESH_IDX, sizeof(unsigned short), nr_vert), mesh);
        if (opts->tangents) CERR_RET_T(mesh_attr_alloc(mesh, MESH_TANGENTS, sizeof(float) * 4, nr_vert), mesh);
    } else {
        nr_vert += mesh_nr_vx(mesh);
        CERR_RET_T(mesh_attr_resize(mesh, MESH_VX, nr_vert), mesh);
        CERR_RET_T(mesh_attr_resize(mesh, MESH_TX, nr_vert), mesh);
        CERR_RET_T(mesh_attr_resize(mesh, MESH_NORM, nr_vert), mesh);
        CERR_RET_T(mesh_attr_resize(mesh, MESH_IDX, nr_vert), mesh);
        /* the first _prim_begin() decided; every later append follows */
        if (mesh->attr[MESH_TANGENTS].data) CERR_RET_T(mesh_attr_resize(mesh, MESH_TANGENTS, nr_vert), mesh);
    }

    return cresp_val(mesh, mesh);
}

/* map the polygon's bounding box onto the unit UV square, image-style (V down) */
static void extrusion_uv(vec2 out, const float *pt, const vec4 bbox)
{
    float w = bbox[2] - bbox[0], h = bbox[3] - bbox[1];

    out[0] = w > 0.0f ? (pt[0] - bbox[0]) / w : 0.5f;
    out[1] = h > 0.0f ? (bbox[3] - pt[1]) / h : 0.5f;
}

cerr _prim_emit_extrusion(const prim_extrusion *ext, const prim_emit_opts *opts)
{
    if (!ext || !ext->points || !ext->rings || !ext->nr_rings || !opts || !opts->mesh)  return CERR_INVALID_ARGUMENTS;

    struct mesh *mesh = opts->mesh;
    cerr err = CERR_NOMEM;

    bool *holes = mem_alloc(sizeof(*holes), .nr = ext->nr_rings);
    if (!holes)     return CERR_NOMEM;

    /* a quad (2 triangles) per edge of each ring the triangulator keeps */
    size_t nr_side_vx = 0;
    for (size_t i = 0; i < ext->nr_rings; i++)
        if (ext->rings[i].nr >= 3)  nr_side_vx += ext->rings[i].nr * 6;

    err = poly_rings_classify(ext->points, ext->rings, ext->nr_rings, holes);
    if (IS_CERR(err))   goto err_holes;

    cres(poly_tris) tres = poly_triangulate(ext->points, ext->rings, ext->nr_rings);
    if (IS_CERR(tres)) {
        err = cerr_error_cres(tres);
        goto err_holes;
    }

    poly_tris tris = tres.val;

    size_t nv = tris.nr_tris * 3 * (ext->closed ? 2 : 1);
    if (ext->depth > 0.0f)  nv += nr_side_vx;

    err = CERR_OK;
    if (!nv)    goto err_tris;

    cresp(mesh) mres = _prim_begin(nv, opts);
    if (IS_CERR(mres)) {
        err = cerr_error_cres(mres);
        goto err_tris;
    }

    size_t nr_points = 0;
    for (size_t i = 0; i < ext->nr_rings; i++)  nr_points = max(nr_points, ext->rings[i].start + ext->rings[i].nr);

    vec4 bbox = { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t i = 0; i < nr_points; i++) {
        bbox[0] = fminf(bbox[0], ext->points[i][0]);
        bbox[1] = fminf(bbox[1], ext->points[i][1]);
        bbox[2] = fmaxf(bbox[2], ext->points[i][0]);
        bbox[3] = fmaxf(bbox[3], ext->points[i][1]);
    }

    prim_emit_opts po = { .mesh = mesh };
    vec2 uv[3];
    vec3 v[3];

    /* the front face */
    for (size_t t = 0; t < tris.nr_tris; t++) {
        const unsigned int *ti = &tris.idx[t * 3];

        for (int k = 0; k < 3; k++) {
            const float *pt = ext->points[ti[k]];

            v[k][0] = pt[0];
            v[k][1] = pt[1];
            v[k][2] = ext->z;
            extrusion_uv(uv[k], pt, bbox);
        }

        po.uv = uv;
        _prim_emit_triangle3(v[0], v[1], v[2], &po);
    }

    /* the back face: mirrored winding, pushed @depth back */
    if (ext->closed)
        for (size_t t = 0; t < tris.nr_tris; t++) {
            const unsigned int *ti = &tris.idx[t * 3];
            vec2 ruv[3];

            for (int k = 0; k < 3; k++) {
                const float *pt = ext->points[ti[k]];

                v[k][0] = pt[0];
                v[k][1] = pt[1];
                v[k][2] = ext->z - ext->depth;
                extrusion_uv(uv[k], pt, bbox);
            }

            vec2_dup(ruv[0], uv[0]);
            vec2_dup(ruv[1], uv[2]);
            vec2_dup(ruv[2], uv[1]);
            po.uv = ruv;
            _prim_emit_triangle3(v[0], v[2], v[1], &po);
        }

    /* the side faces: a quad per ring edge, facing away from the fill */
    if (ext->depth > 0.0f) {
        po.uv = nullptr;

        for (size_t i = 0; i < ext->nr_rings; i++) {
            const poly_ring *ring = &ext->rings[i];
            if (ring->nr < 3)   continue;

            bool ccw = poly_ring_area(ext->points, ring) > 0.0f;
            /* clockwise holes (like counterclockwise fills) face outward as is */
            bool flip = ccw == holes[i];

            for (size_t k = 0; k < ring->nr; k++) {
                const float *a = ext->points[ring->start + k];
                const float *b = ext->points[ring->start + (k + 1) % ring->nr];
                vec3 quad[4];

                quad[0][0] = flip ? b[0] : a[0];
                quad[0][1] = flip ? b[1] : a[1];
                quad[0][2] = ext->z;
                quad[1][0] = flip ? a[0] : b[0];
                quad[1][1] = flip ? a[1] : b[1];
                quad[1][2] = ext->z;
                quad[2][0] = quad[1][0];
                quad[2][1] = quad[1][1];
                quad[2][2] = ext->z - ext->depth;
                quad[3][0] = quad[0][0];
                quad[3][1] = quad[0][1];
                quad[3][2] = ext->z - ext->depth;

                _prim_emit_quad(quad, &po);
            }
        }
    }

err_tris:
    mem_free(tris.idx);
err_holes:
    mem_free(holes);

    return err;
}

/*
 * The emitter treats every vertex attribute as optional -- no
 * attribute, no problem -- so a failed grow, which nukes the
 * attribute (see mesh_attr_resize()), costs the mesh its tangents
 * while the error still propagates. The guard keeps the resize from
 * conjuring the attribute up on meshes that never had it.
 */
static cerr prim_tangents_resize(struct mesh *mesh, size_t nr_vert)
{
    if (!mesh_tangent(mesh))    return CERR_OK;

    return mesh_attr_resize(mesh, MESH_TANGENTS, nr_vert);
}

cerr _prim_emit_cylinder(vec3 org, float height, float radius, int nr_serments, const prim_emit_opts *opts)
{
    struct mesh *mesh = opts->mesh;
    if (!mesh)  return CERR_INVALID_ARGUMENTS;

    /*
     * a triangle per each segment at the top and bottom, plus 2 triangles (quad)
     * for each side: 4 triangles (12 vertices) per segment
     */
    int nr_vert = nr_serments * 12;
    if (!prim_fits(mesh, nr_vert))  return CERR_TOO_LARGE;

    if (!mesh->attr[MESH_VX].data) {
        CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_VX, sizeof(float) * 3, nr_vert));
        CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_TX, sizeof(float) * 2, nr_vert));
        CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_NORM, sizeof(float) * 3, nr_vert));
        CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_IDX, sizeof(unsigned short), nr_vert));
        if (opts->tangents) CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_TANGENTS, sizeof(float) * 4, nr_vert));
    } else {
        nr_vert += mesh_nr_vx(mesh);
        CERR_RET_CERR(mesh_attr_resize(mesh, MESH_VX, nr_vert));
        CERR_RET_CERR(mesh_attr_resize(mesh, MESH_TX, nr_vert));
        CERR_RET_CERR(mesh_attr_resize(mesh, MESH_NORM, nr_vert));
        CERR_RET_CERR(mesh_attr_resize(mesh, MESH_IDX, nr_vert));
        CERR_RET_CERR(prim_tangents_resize(mesh, nr_vert));
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

    return CERR_OK;
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

cerr _prim_emit_sphere(vec3 org, float radius, int nr_serments, const prim_emit_opts *opts)
{
    struct mesh *mesh = opts->mesh;
    if (!mesh)  return CERR_INVALID_ARGUMENTS;

    if (nr_serments < 3)
        nr_serments = 3;

    int nr_stacks = nr_serments / 2;
    if (nr_stacks < 2)
        nr_stacks = 2;

    int nr_vert = nr_serments * 6 * (nr_stacks - 1);
    if (!prim_fits(mesh, nr_vert))  return CERR_TOO_LARGE;

    if (!mesh->attr[MESH_VX].data) {
        CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_VX, sizeof(float) * 3, nr_vert));
        CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_TX, sizeof(float) * 2, nr_vert));
        CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_NORM, sizeof(float) * 3, nr_vert));
        CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_IDX, sizeof(unsigned short), nr_vert));
        if (opts->tangents) CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_TANGENTS, sizeof(float) * 4, nr_vert));
    } else {
        nr_vert += mesh_nr_vx(mesh);
        CERR_RET_CERR(mesh_attr_resize(mesh, MESH_VX, nr_vert));
        CERR_RET_CERR(mesh_attr_resize(mesh, MESH_TX, nr_vert));
        CERR_RET_CERR(mesh_attr_resize(mesh, MESH_NORM, nr_vert));
        CERR_RET_CERR(mesh_attr_resize(mesh, MESH_IDX, nr_vert));
        CERR_RET_CERR(prim_tangents_resize(mesh, nr_vert));
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

    return CERR_OK;
}

/* a point on a ring of radius @r around @c, in the (@e0, @e1) plane */
static void prim_ring_pt(vec3 out, const vec3 c, const vec3 e0, const vec3 e1,
                         float r, float ang)
{
    float ca = cosf(ang) * r, sa = sinf(ang) * r;

    out[0] = c[0] + e0[0] * ca + e1[0] * sa;
    out[1] = c[1] + e0[1] * ca + e1[1] * sa;
    out[2] = c[2] + e0[2] * ca + e1[2] * sa;
}

cerr _prim_emit_prism(vec3 a, vec3 b, float ra, float rb, int sides, float phase,
                      const prim_emit_opts *opts)
{
    vec3 axis, e0, e1, up = { 0.0f, 1.0f, 0.0f };

    if (!opts->mesh || sides < 3)   return CERR_INVALID_ARGUMENTS;

    vec3_sub(axis, b, a);
    float len = vec3_len(axis);
    if (len < 1e-5f)    return CERR_OK;
    vec3_scale(axis, axis, 1.0f / len);

    if (fabsf(vec3_mul_inner(axis, up)) > 0.9f) {
        up[0] = 1.0f; up[1] = 0.0f; up[2] = 0.0f;
    }
    vec3_mul_cross(e0, up, axis);
    vec3_norm(e0, e0);
    vec3_mul_cross(e1, axis, e0);
    vec3_norm(e1, e1);

    /* per side: 2 wall + 2 cap triangles => sides * 12 vertices */
    CRES_RET_CERR(_prim_begin(sides * 12, opts));

    for (int s = 0; s < sides; s++) {
        float t0 = phase + (float)s / sides * 2.0f * (float)M_PI;
        float t1 = phase + (float)(s + 1) / sides * 2.0f * (float)M_PI;
        vec3 a0, a1, b0, b1;

        prim_ring_pt(a0, a, e0, e1, ra, t0);
        prim_ring_pt(a1, a, e0, e1, ra, t1);
        prim_ring_pt(b0, b, e0, e1, rb, t0);
        prim_ring_pt(b1, b, e0, e1, rb, t1);

        _prim_emit_triangle3(a0, a1, b1, opts);     /* wall, wound outward */
        _prim_emit_triangle3(a0, b1, b0, opts);
        _prim_emit_triangle3(a, a1, a0, opts);      /* near cap */
        _prim_emit_triangle3(b, b0, b1, opts);      /* far cap */
    }

    return CERR_OK;
}

static cerr triangle_split(struct mesh *mesh, size_t tri, float off)
{
    size_t nr_vx = mesh_nr_vx(mesh);
    size_t nr_idx = mesh_nr_idx(mesh);

    /* the 16-bit index ceiling declines the split, it doesn't fail it */
    if (!prim_fits(mesh, 6))    return CERR_OK;

    CERR_RET_CERR(mesh_attr_resize(mesh, MESH_VX, nr_vx + 6));
    CERR_RET_CERR(mesh_attr_resize(mesh, MESH_TX, nr_vx + 6));
    CERR_RET_CERR(mesh_attr_resize(mesh, MESH_NORM, nr_vx + 6));
    CERR_RET_CERR(mesh_attr_resize(mesh, MESH_IDX, nr_idx + 6));
    CERR_RET_CERR(prim_tangents_resize(mesh, nr_vx + 6));

    size_t idx = tri * 3;
    size_t vx_a = mesh_idx(mesh)[idx + 0];
    size_t vx_b = mesh_idx(mesh)[idx + 1];
    size_t vx_c = mesh_idx(mesh)[idx + 2];
    vec3 a, b, c, center;
    vec3_dup(a, &mesh_vx(mesh)[vx_a * 3]);
    vec3_dup(b, &mesh_vx(mesh)[vx_b * 3]);
    vec3_dup(c, &mesh_vx(mesh)[vx_c * 3]);

    vec3 norm;
    vec3_dup(norm, &mesh_norm(mesh)[vx_a * 3]);

    vec3_add(center, a, b);
    vec3_add(center, center, c);
    vec3_scale(center, center, 1.0 / 3.0);
    vec3_add_scaled(center, center, norm, 1.0, off);

    /* replace c with center in the original triangle: a -> b -> center */
    vec3_dup(&mesh_vx(mesh)[vx_c * 3], center);
    _prim_calc_normals(vx_a, &(prim_emit_opts){ .mesh = mesh });

    /* new triangle: b -> c -> center */
    _prim_emit_triangle3(b, c, center, &(prim_emit_opts){ .mesh = mesh });
    /* new triangle: c -> a -> center */
    _prim_emit_triangle3(c, a, center, &(prim_emit_opts){ .mesh = mesh });

    return CERR_OK;
}

cerr mesh_tessellate(struct mesh *mesh, int total_depth, mesh_tess_fn fn, void *data)
{
    mesh_tess_params params = {
        .mesh        = mesh,
        .total_depth = total_depth,
    };

    for (int depth = 0; depth < total_depth; depth++) {
        size_t nr_tris = mesh_nr_idx(mesh) / 3;

        params.depth = depth;

        for (size_t i = 0; i < nr_tris; i++) {
            size_t vx_a = mesh_idx(mesh)[i * 3];
            vec3_dup(params.normal, &mesh_norm(mesh)[vx_a * 3]);
            params.tri = i;

            float off = fn(&params, data);
            if (off != 0.0f)
                CERR_RET_CERR(triangle_split(mesh, i, off));
        }
    }

    return CERR_OK;
}
