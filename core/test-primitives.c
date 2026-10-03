// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
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

static const struct test tests[] = {
    { .name = "primitives 16-bit guard", .test = prim_guard_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
