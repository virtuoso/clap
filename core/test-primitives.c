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

static bool prim_vec_near(const float *a, const float *b, int n)
{
    for (int i = 0; i < n; i++)
        if (fabsf(a[i] - b[i]) > PRIM_TEST_EPS) return false;

    return true;
}

/*
 * The tangent of vertex @i is unit length, perpendicular to its normal,
 * w = +-1, and the bitangent the shader rebuilds, cross(n, t) * w, runs
 * along @vdir, the direction V grows in
 */
static bool prim_tangent_ok(struct mesh *m, size_t i, const vec3 vdir)
{
    float *n = &mesh_norm(m)[i * 3], *t = &mesh_tangent(m)[i * 4];
    vec3 b;

    if (fabsf(vec3_len(t) - 1.0f) > PRIM_TEST_EPS)          return false;
    if (fabsf(vec3_mul_inner(t, n)) > PRIM_TEST_EPS)        return false;
    if (t[3] != 1.0f && t[3] != -1.0f)                      return false;

    vec3_mul_cross(b, n, t);
    vec3_scale(b, b, t[3]);

    return vec3_mul_inner(b, vdir) > 0.0f;
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
            vec3 n;

            vec3_scale(n, normal, cw ? -1.0f : 1.0f);
            if (!prim_vec_near(&mesh_vx(m)[i * 3], p, 3))                       return EXIT_FAILURE;
            if (!prim_vec_near(&mesh_tx(m)[i * 2], uv, 2))                      return EXIT_FAILURE;
            if (!prim_vec_near(&mesh_norm(m)[i * 3], n, 3))                     return EXIT_FAILURE;
            if (!prim_vec_near(&mesh_tangent(m)[i * 4], udir, 3))               return EXIT_FAILURE;
            if (!prim_tangent_ok(m, i, vdir))                                   return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "primitives 16-bit guard", .test = prim_guard_test },
    { .name = "primitives face", .test = prim_face_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
