// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include "mesh.h"
#include "primitives.h"
#include "test.h"
#include "util.h"

/* fill a fresh mesh with @nr vertices (a multiple of 3) through the emitter */
static cerr guard_mesh_fill(struct mesh *mesh, size_t nr)
{
    vec3 a = { 0, 0, 0 }, b = { 1, 0, 0 }, c = { 0, 0, 1 };

    CRES_RET_CERR(prim_begin(nr, .mesh = mesh));

    for (size_t i = 0; i < nr; i += 3)  prim_emit_triangle3(a, b, c, .mesh = mesh);

    return mesh_nr_vx(mesh) == nr ? CERR_OK : CERR_INVALID_OPERATION;
}

/* the vertex and index counts and the storage of @mesh haven't moved */
static bool guard_mesh_same(struct mesh *mesh, size_t nr_vx, const float *vx, const unsigned short *idx)
{
    return mesh_nr_vx(mesh) == nr_vx && mesh_nr_idx(mesh) == nr_vx && mesh_nr_norm(mesh) == nr_vx &&
           mesh_vx(mesh) == vx && mesh_idx(mesh) == idx;
}

static int prim_guard_test(void)
{
    /* 65520 is the last multiple of 36 below USHRT_MAX: one more box doesn't fit */
    const size_t nr_full = 65520;

    LOCAL_SET(mesh_t, mesh) = CRES_RET(ref_new_checked(mesh, .name = "guard"), return EXIT_FAILURE);
    CERR_RET(guard_mesh_fill(mesh, nr_full), return EXIT_FAILURE);

    const float *vx = mesh_vx(mesh);
    const unsigned short *idx = mesh_idx(mesh);
    vec3 org = { 0, 0, 0 }, top = { 0, 1, 0 };

    cresp(mesh) mres = prim_begin(36, .mesh = mesh);
    if (!IS_CERR_CODE(mres, CERR_TOO_LARGE))                            return EXIT_FAILURE;
    if (!guard_mesh_same(mesh, nr_full, vx, idx))                       return EXIT_FAILURE;

    /* 3 segments are 36 vertices */
    cerr err = prim_emit_cylinder(org, 1.0f, 1.0f, 3, .mesh = mesh);
    if (!IS_CERR_CODE(err, CERR_TOO_LARGE))                             return EXIT_FAILURE;
    if (!guard_mesh_same(mesh, nr_full, vx, idx))                       return EXIT_FAILURE;

    /* 4 segments and 2 stacks are 24 vertices */
    err = prim_emit_sphere(org, 1.0f, 4, .mesh = mesh);
    if (!IS_CERR_CODE(err, CERR_TOO_LARGE))                             return EXIT_FAILURE;
    if (!guard_mesh_same(mesh, nr_full, vx, idx))                       return EXIT_FAILURE;

    /* 3 sides are 36 vertices, through _prim_begin() */
    err = prim_emit_prism(org, top, 1.0f, 1.0f, 3, 0.0f, .mesh = mesh);
    if (!IS_CERR_CODE(err, CERR_TOO_LARGE))                             return EXIT_FAILURE;
    if (!guard_mesh_same(mesh, nr_full, vx, idx))                       return EXIT_FAILURE;

    /* the last 15 still fit */
    CRES_RET(prim_begin(USHRT_MAX - nr_full, .mesh = mesh), return EXIT_FAILURE);

    /* a new mesh that would be too large is refused before it is made */
    mres = prim_begin(USHRT_MAX + 1, .name = "too large");
    if (!IS_CERR_CODE(mres, CERR_TOO_LARGE))                            return EXIT_FAILURE;

    /* an empty mesh takes the whole range */
    LOCAL_SET(mesh_t, empty) = CRES_RET(ref_new_checked(mesh, .name = "empty"), return EXIT_FAILURE);
    CRES_RET(prim_begin(USHRT_MAX, .mesh = empty), return EXIT_FAILURE);

    return EXIT_SUCCESS;
}

/** PRIM_TEST_EPS - tolerance of the attribute comparisons */
#define PRIM_TEST_EPS 1e-5f

static bool prim_vec_near(const float *a, const float *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (fabsf(a[i] - b[i]) > PRIM_TEST_EPS) return false;

    return true;
}

/* the tangent of vertex @i is unit length, perpendicular to its normal, w = +-1 */
static bool prim_tangent_ok(struct mesh *m, size_t i)
{
    float *n = &mesh_norm(m)[i * 3], *t = &mesh_tangent(m)[i * 4];

    if (fabsf(vec3_len(t) - 1.0f) > PRIM_TEST_EPS)          return false;
    if (fabsf(vec3_mul_inner(t, n)) > PRIM_TEST_EPS)        return false;

    return t[3] == 1.0f || t[3] == -1.0f;
}

/* the bitangent the shader rebuilds for vertex @i, cross(n, t) * w: where V grows */
static void prim_bitangent(struct mesh *m, size_t i, vec3 b)
{
    float *n = &mesh_norm(m)[i * 3], *t = &mesh_tangent(m)[i * 4];

    vec3_mul_cross(b, n, t);
    vec3_scale(b, b, t[3]);
}

static int prim_face_test(void)
{
    /* a tilted 2 x 1.5 rectangle: U along (0.6, 0, 0.8), V along +y */
    vec3 c00 = { 1.0f, 2.0f, 3.0f }, c10 = { 2.2f, 2.0f, 4.6f };
    vec3 c11 = { 2.2f, 3.5f, 4.6f }, c01 = { 1.0f, 3.5f, 3.0f };
    vec4 rect = { 0.25f, -1.0f, 0.75f, 2.0f };
    /* cross(c01 - c00, c10 - c00), normalized */
    vec3 normal = { 0.8f, 0.0f, -0.6f }, udir = { 0.6f, 0.0f, 0.8f }, vdir = { 0.0f, 1.0f, 0.0f };

    for (int cw = 0; cw < 2; cw++) {
        LOCAL_SET(mesh_t, m) = CRES_RET(ref_new_checked(mesh, .name = "face"), return EXIT_FAILURE);
        CRES_RET(prim_begin(6, .mesh = m, .tangents = true), return EXIT_FAILURE);
        prim_emit_face(c00, c10, c11, c01, rect, .mesh = m, .clockwise = cw);
        if (mesh_nr_vx(m) != 6)                                                 return EXIT_FAILURE;

        /* (c00, c01, c10) and (c01, c11, c10); clockwise swaps each triangle's last two */
        const float *pos[2][6] = {
            { c00, c01, c10, c01, c11, c10 },
            { c00, c10, c01, c01, c10, c11 },
        };
        for (size_t i = 0; i < 6; i++) {
            const float *p = pos[cw][i];
            vec2 uv = {
                p == c00 || p == c01 ? rect[0] : rect[2],
                p == c00 || p == c10 ? rect[1] : rect[3],
            };
            vec3 n, b;

            vec3_scale(n, normal, cw ? -1.0f : 1.0f);
            prim_bitangent(m, i, b);
            if (!prim_vec_near(&mesh_vx(m)[i * 3], p, 3))                       return EXIT_FAILURE;
            if (!prim_vec_near(&mesh_tx(m)[i * 2], uv, 2))                      return EXIT_FAILURE;
            if (!prim_vec_near(&mesh_norm(m)[i * 3], n, 3))                     return EXIT_FAILURE;
            if (!prim_vec_near(&mesh_tangent(m)[i * 4], udir, 3))               return EXIT_FAILURE;
            if (!prim_tangent_ok(m, i))                                         return EXIT_FAILURE;
            if (!prim_vec_near(b, vdir, 3))                                     return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

/*
 * Reference copies of the original box emitters, whose output
 * prim_emit_box() keeps: an axis-aligned box with planar UVs, the same
 * in a yawed run frame, and a box with every face pinned to one UV
 * rectangle
 */
static void arch_face(struct mesh *m, vec3 c00, vec3 c10, vec3 c11, vec3 c01,
                      float u0, float v0, float u1, float v1)
{
    prim_emit_opts opts = { .mesh = m };
    vec2 t0[3] = { { u0, v0 }, { u0, v1 }, { u1, v0 } };
    vec2 t1[3] = { { u0, v1 }, { u1, v1 }, { u1, v0 } };

    opts.uv = t0;
    _prim_emit_triangle3(c00, c01, c10, &opts);
    opts.uv = t1;
    _prim_emit_triangle3(c01, c11, c10, &opts);
}

static cerr arch_box(struct mesh *m, vec3 lo, vec3 hi, float uv)
{
    CRES_RET_CERR(_prim_begin(36, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    float x0 = lo[0], y0 = lo[1], z0 = lo[2];
    float x1 = hi[0], y1 = hi[1], z1 = hi[2];

    /* +x: U +z, V +y */
    arch_face(m, (vec3){ x1, y0, z0 }, (vec3){ x1, y0, z1 }, (vec3){ x1, y1, z1 },
              (vec3){ x1, y1, z0 }, z0 * uv, y0 * uv, z1 * uv, y1 * uv);
    /* -x: U -z, V +y */
    arch_face(m, (vec3){ x0, y0, z1 }, (vec3){ x0, y0, z0 }, (vec3){ x0, y1, z0 },
              (vec3){ x0, y1, z1 }, -z1 * uv, y0 * uv, -z0 * uv, y1 * uv);
    /* +y: U +x, V +z */
    arch_face(m, (vec3){ x0, y1, z0 }, (vec3){ x1, y1, z0 }, (vec3){ x1, y1, z1 },
              (vec3){ x0, y1, z1 }, x0 * uv, z0 * uv, x1 * uv, z1 * uv);
    /* -y: U +z, V +x */
    arch_face(m, (vec3){ x0, y0, z0 }, (vec3){ x0, y0, z1 }, (vec3){ x1, y0, z1 },
              (vec3){ x1, y0, z0 }, z0 * uv, x0 * uv, z1 * uv, x1 * uv);
    /* +z: U -x, V +y */
    arch_face(m, (vec3){ x1, y0, z1 }, (vec3){ x0, y0, z1 }, (vec3){ x0, y1, z1 },
              (vec3){ x1, y1, z1 }, -x1 * uv, y0 * uv, -x0 * uv, y1 * uv);
    /* -z: U +x, V +y */
    arch_face(m, (vec3){ x0, y0, z0 }, (vec3){ x1, y0, z0 }, (vec3){ x1, y1, z0 },
              (vec3){ x0, y1, z0 }, x0 * uv, y0 * uv, x1 * uv, y1 * uv);

    return CERR_OK;
}

static cerr arch_dir_box(struct mesh *m, const vec2 org, const vec2 dir,
                         float a0, float a1, float y0, float y1, float c0, float c1, float uv)
{
    if (a0 > a1) { float t = a0; a0 = a1; a1 = t; }

    float px = -dir[1], pz = dir[0];
    CRES_RET_CERR(_prim_begin(36, &(prim_emit_opts){ .mesh = m, .tangents = true }));

#define DIRPT(_a, _y, _c) \
    (vec3){ org[0] + (_a) * dir[0] + (_c) * px, (_y), org[1] + (_a) * dir[1] + (_c) * pz }
    /* +a end: U +c, V +y */
    arch_face(m, DIRPT(a1, y0, c0), DIRPT(a1, y0, c1), DIRPT(a1, y1, c1), DIRPT(a1, y1, c0),
              c0 * uv, y0 * uv, c1 * uv, y1 * uv);
    /* -a end: U -c, V +y */
    arch_face(m, DIRPT(a0, y0, c1), DIRPT(a0, y0, c0), DIRPT(a0, y1, c0), DIRPT(a0, y1, c1),
              -c1 * uv, y0 * uv, -c0 * uv, y1 * uv);
    /* top: U +a, V +c */
    arch_face(m, DIRPT(a0, y1, c0), DIRPT(a1, y1, c0), DIRPT(a1, y1, c1), DIRPT(a0, y1, c1),
              a0 * uv, c0 * uv, a1 * uv, c1 * uv);
    /* bottom: U +c, V +a */
    arch_face(m, DIRPT(a0, y0, c0), DIRPT(a0, y0, c1), DIRPT(a1, y0, c1), DIRPT(a1, y0, c0),
              c0 * uv, a0 * uv, c1 * uv, a1 * uv);
    /* +c side: U -a, V +y */
    arch_face(m, DIRPT(a1, y0, c1), DIRPT(a0, y0, c1), DIRPT(a0, y1, c1), DIRPT(a1, y1, c1),
              -a1 * uv, y0 * uv, -a0 * uv, y1 * uv);
    /* -c side: U +a, V +y */
    arch_face(m, DIRPT(a0, y0, c0), DIRPT(a1, y0, c0), DIRPT(a1, y1, c0), DIRPT(a0, y1, c0),
              a0 * uv, y0 * uv, a1 * uv, y1 * uv);
#undef DIRPT

    return CERR_OK;
}

static void furn_face(struct mesh *m, vec3 c00, vec3 c10, vec3 c11, vec3 c01,
                      float u0, float v0, float u1, float v1)
{
    prim_emit_opts opts = { .mesh = m };
    vec2 t0[3] = { { u0, v0 }, { u0, v1 }, { u1, v0 } };
    vec2 t1[3] = { { u0, v1 }, { u1, v1 }, { u1, v0 } };

    opts.uv = t0;
    _prim_emit_triangle3(c00, c01, c10, &opts);
    opts.uv = t1;
    _prim_emit_triangle3(c01, c11, c10, &opts);
}

static cerr furn_box_uv(struct mesh *m, vec3 lo, vec3 hi, float u0, float v0, float u1, float v1)
{
    CRES_RET_CERR(_prim_begin(36, &(prim_emit_opts){ .mesh = m }));

    float x0 = lo[0], y0 = lo[1], z0 = lo[2];
    float x1 = hi[0], y1 = hi[1], z1 = hi[2];

    furn_face(m, (vec3){ x1, y0, z0 }, (vec3){ x1, y0, z1 }, (vec3){ x1, y1, z1 },
              (vec3){ x1, y1, z0 }, u0, v0, u1, v1);
    furn_face(m, (vec3){ x0, y0, z1 }, (vec3){ x0, y0, z0 }, (vec3){ x0, y1, z0 },
              (vec3){ x0, y1, z1 }, u0, v0, u1, v1);
    furn_face(m, (vec3){ x0, y1, z0 }, (vec3){ x1, y1, z0 }, (vec3){ x1, y1, z1 },
              (vec3){ x0, y1, z1 }, u0, v0, u1, v1);
    furn_face(m, (vec3){ x0, y0, z0 }, (vec3){ x0, y0, z1 }, (vec3){ x1, y0, z1 },
              (vec3){ x1, y0, z0 }, u0, v0, u1, v1);
    furn_face(m, (vec3){ x1, y0, z1 }, (vec3){ x0, y0, z1 }, (vec3){ x0, y1, z1 },
              (vec3){ x1, y1, z1 }, u0, v0, u1, v1);
    furn_face(m, (vec3){ x0, y0, z0 }, (vec3){ x1, y0, z0 }, (vec3){ x1, y1, z0 },
              (vec3){ x0, y1, z0 }, u0, v0, u1, v1);

    return CERR_OK;
}

/* a new mesh holding @box, with tangents if @tangents */
static cresp(mesh) prim_box_mesh(const prim_box *box, uint64_t skip, bool tangents)
{
    LOCAL_SET(mesh_t, m) = CRES_RET_T(ref_new_checked(mesh, .name = "box"), mesh);
    CERR_RET_T(prim_emit_box(box, .mesh = m, .skip_mask = skip, .tangents = tangents), mesh);

    return cresp_val(mesh, NOCU(m));
}

/* @nr vertices of @a from @ia on match those of @b from @ib, attribute by attribute */
static bool prim_vx_near(struct mesh *a, size_t ia, struct mesh *b, size_t ib, size_t nr)
{
    if (!prim_vec_near(&mesh_vx(a)[ia * 3], &mesh_vx(b)[ib * 3], nr * 3))       return false;
    if (!prim_vec_near(&mesh_tx(a)[ia * 2], &mesh_tx(b)[ib * 2], nr * 2))       return false;
    if (!prim_vec_near(&mesh_norm(a)[ia * 3], &mesh_norm(b)[ib * 3], nr * 3))   return false;
    if (!mesh_tangent(a) != !mesh_tangent(b))                                   return false;

    return !mesh_tangent(a) || prim_vec_near(&mesh_tangent(a)[ia * 4], &mesh_tangent(b)[ib * 4], nr * 4);
}

/* @a and @b hold the same vertices in the same order */
static bool prim_mesh_near(struct mesh *a, struct mesh *b)
{
    return mesh_nr_vx(a) == mesh_nr_vx(b) && prim_vx_near(a, 0, b, 0, mesh_nr_vx(a));
}

static const prim_box prim_test_box = {
    .org    = { 5.0f, -2.0f },
    .dir    = { 1.0f, 0.0f },
    .lo     = { -0.4f, 0.5f, -1.3f },
    .hi     = { 2.1f, 3.0f, 0.7f },
    .uv     = 1.0f / 2.4f,
};

static int prim_box_skip_test(void)
{
    LOCAL_SET(mesh_t, full) = CRES_RET(prim_box_mesh(&prim_test_box, 0, true), return EXIT_FAILURE);
    if (mesh_nr_vx(full) != 36)                                             return EXIT_FAILURE;

    /* each skip bit drops its face's 6 vertices and leaves the rest in order */
    for (uint64_t skip = 0; skip < 64; skip++) {
        LOCAL_SET(mesh_t, m) = CRES_RET(prim_box_mesh(&prim_test_box, skip, true), return EXIT_FAILURE);
        size_t nr = 0;

        for (int face = 0; face < 6; face++) {
            if (skip & (1ull << face))                                      continue;
            if (!prim_vx_near(m, nr, full, face * 6, 6))                    return EXIT_FAILURE;
            nr += 6;
        }
        if (mesh_nr_vx(m) != nr || nr != 36 - 6 * (size_t)__builtin_popcountll(skip))   return EXIT_FAILURE;
    }

    /* the box reserves only the faces it emits: 30 vertices fill the last 30 */
    LOCAL_SET(mesh_t, mesh) = CRES_RET(ref_new_checked(mesh, .name = "guard"), return EXIT_FAILURE);
    CERR_RET(guard_mesh_fill(mesh, USHRT_MAX - 30), return EXIT_FAILURE);
    CERR_RET(prim_emit_box(&prim_test_box, .mesh = mesh, .skip_mask = PRIM_BOX_NEG_Y), return EXIT_FAILURE);
    if (mesh_nr_vx(mesh) != USHRT_MAX)                                      return EXIT_FAILURE;

    const float *vx = mesh_vx(mesh);
    const unsigned short *idx = mesh_idx(mesh);
    cerr err = prim_emit_box(&prim_test_box, .mesh = mesh, .skip_mask = 0x3e);
    if (!IS_CERR_CODE(err, CERR_TOO_LARGE))                                 return EXIT_FAILURE;
    if (!guard_mesh_same(mesh, USHRT_MAX, vx, idx))                         return EXIT_FAILURE;

    /* all faces skipped: nothing to do */
    CERR_RET(prim_emit_box(&prim_test_box, .mesh = mesh, .skip_mask = 0x3f), return EXIT_FAILURE);

    err = prim_emit_box(&prim_test_box);
    if (!IS_CERR_CODE(err, CERR_INVALID_ARGUMENTS))                         return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int prim_box_yaw_test(void)
{
    const float yaws[] = { 0.0f, 30.0f, 90.0f, 180.0f, 247.0f };
    const prim_box *b = &prim_test_box;
    float ac = (b->lo[0] + b->hi[0]) / 2.0f, yc = (b->lo[1] + b->hi[1]) / 2.0f;
    float cc = (b->lo[2] + b->hi[2]) / 2.0f;
    float volume = (b->hi[0] - b->lo[0]) * (b->hi[1] - b->lo[1]) * (b->hi[2] - b->lo[2]);

    for (size_t i = 0; i < array_size(yaws); i++) {
        float yaw = yaws[i] * (float)M_PI / 180.0f;
        prim_box box = *b;

        box.dir[0] = cosf(yaw);
        box.dir[1] = sinf(yaw);

        LOCAL_SET(mesh_t, m) = CRES_RET(prim_box_mesh(&box, 0, true), return EXIT_FAILURE);
        vec3 center = {
            box.org[0] + ac * box.dir[0] - cc * box.dir[1],
            yc,
            box.org[1] + ac * box.dir[1] + cc * box.dir[0],
        };
        /* the +a end comes first and faces along the run */
        vec3 pos_a = { box.dir[0], 0.0f, box.dir[1] };
        if (!prim_vec_near(mesh_norm(m), pos_a, 3))                         return EXIT_FAILURE;

        /* every triangle faces away from the center; the divergence theorem gives the volume */
        float signed_volume = 0.0f;
        for (size_t t = 0; t < mesh_nr_vx(m); t += 3) {
            float *v = &mesh_vx(m)[t * 3], *n = &mesh_norm(m)[t * 3];
            vec3 centroid, out, cross;

            vec3_add(centroid, &v[0], &v[3]);
            vec3_add(centroid, centroid, &v[6]);
            vec3_scale(centroid, centroid, 1.0f / 3.0f);
            vec3_sub(out, centroid, center);
            if (vec3_mul_inner(out, n) <= 0.0f)                             return EXIT_FAILURE;
            if (fabsf(vec3_len(n) - 1.0f) > PRIM_TEST_EPS)                  return EXIT_FAILURE;

            vec3_mul_cross(cross, &v[3], &v[6]);
            signed_volume += vec3_mul_inner(&v[0], cross) / 6.0f;

            for (size_t k = 0; k < 3; k++)
                if (!prim_tangent_ok(m, t + k))                             return EXIT_FAILURE;
        }
        if (fabsf(signed_volume - volume) > 1e-3f)                          return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int prim_box_reversed_test(void)
{
    LOCAL_SET(mesh_t, ref) = CRES_RET(prim_box_mesh(&prim_test_box, 0, true), return EXIT_FAILURE);

    /* every combination of reversed (a, y, c) ranges gives the same box */
    for (int axes = 1; axes < 8; axes++) {
        prim_box box = prim_test_box;

        for (int k = 0; k < 3; k++)
            if (axes & (1 << k)) {
                box.lo[k] = prim_test_box.hi[k];
                box.hi[k] = prim_test_box.lo[k];
            }

        LOCAL_SET(mesh_t, m) = CRES_RET(prim_box_mesh(&box, 0, true), return EXIT_FAILURE);
        if (!prim_mesh_near(m, ref))                                        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int prim_box_chart_test(void)
{
    /* the identity frame is arch_box() */
    prim_box box = prim_test_box;
    box.org[0] = box.org[1] = 0.0f;

    LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "arch_box"), return EXIT_FAILURE);
    CERR_RET(arch_box(ref, box.lo, box.hi, box.uv), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, m) = CRES_RET(prim_box_mesh(&box, 0, true), return EXIT_FAILURE);
    if (!prim_mesh_near(m, ref))                                            return EXIT_FAILURE;

    /* a yawed, offset frame with a reversed run is arch_dir_box() */
    float yaw = 247.0f * (float)M_PI / 180.0f;
    box = prim_test_box;
    box.dir[0] = cosf(yaw);
    box.dir[1] = sinf(yaw);
    box.lo[0] = prim_test_box.hi[0];
    box.hi[0] = prim_test_box.lo[0];

    LOCAL_SET(mesh_t, dref) = CRES_RET(ref_new_checked(mesh, .name = "arch_dir_box"), return EXIT_FAILURE);
    CERR_RET(arch_dir_box(dref, box.org, box.dir, box.lo[0], box.hi[0], box.lo[1], box.hi[1],
                          box.lo[2], box.hi[2], box.uv),
             return EXIT_FAILURE);
    LOCAL_SET(mesh_t, dm) = CRES_RET(prim_box_mesh(&box, 0, true), return EXIT_FAILURE);
    if (!prim_mesh_near(dm, dref))                                          return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int prim_box_rect_test(void)
{
    const vec4 rect = { 0.1f, 0.2f, 0.4f, 0.8f };
    prim_box box = prim_test_box;
    box.org[0] = box.org[1] = 0.0f;
    box.uv_rect = rect;

    /* every face pinned to one rectangle is furn_box_uv() */
    LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "furn_box_uv"), return EXIT_FAILURE);
    CERR_RET(furn_box_uv(ref, box.lo, box.hi, rect[0], rect[1], rect[2], rect[3]), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, m) = CRES_RET(prim_box_mesh(&box, 0, false), return EXIT_FAILURE);
    if (!prim_mesh_near(m, ref))                                            return EXIT_FAILURE;

    for (size_t i = 0; i < mesh_nr_vx(m); i++) {
        float *uv = &mesh_tx(m)[i * 2];

        if (uv[0] != rect[0] && uv[0] != rect[2])                           return EXIT_FAILURE;
        if (uv[1] != rect[1] && uv[1] != rect[3])                           return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "primitives 16-bit guard", .test = prim_guard_test },
    { .name = "primitives face", .test = prim_face_test },
    { .name = "primitives box skip", .test = prim_box_skip_test },
    { .name = "primitives box yaw", .test = prim_box_yaw_test },
    { .name = "primitives box reversed", .test = prim_box_reversed_test },
    { .name = "primitives box chart", .test = prim_box_chart_test },
    { .name = "primitives box uv rect", .test = prim_box_rect_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
