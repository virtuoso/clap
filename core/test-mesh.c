// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"
#include "test.h"
#include "util.h"

/* a resize to zero empties an attribute on every C library, and it grows again from there */
static int mesh_attr_resize_zero_test(void)
{
    LOCAL_SET(mesh_t, m) = CRES_RET(ref_new_checked(mesh, .name = "resize"), return EXIT_FAILURE);
    struct mesh_attr *ma = mesh_attr(m, MESH_VX);

    CERR_RET(mesh_attr_alloc(m, MESH_VX, sizeof(float) * 3, 4), return EXIT_FAILURE);
    CERR_RET(mesh_attr_resize(m, MESH_VX, 0), return EXIT_FAILURE);
    if (ma->data || ma->nr)                             return EXIT_FAILURE;

    CERR_RET(mesh_attr_resize(m, MESH_VX, 0), return EXIT_FAILURE);
    CERR_RET(mesh_attr_resize(m, MESH_VX, 4), return EXIT_FAILURE);
    if (!ma->data || ma->stride != sizeof(float) * 3)   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* A grid of @grid x @grid quads over a rolling height field: something to simplify */
static struct mesh *grid_mesh(int grid)
{
    size_t nr_vx = (grid + 1) * (grid + 1), nr_idx = 0;
    float *vx = mem_alloc(sizeof(float) * 3, .nr = nr_vx, .fatal_fail = 1);
    unsigned short *idx = mem_alloc(sizeof(unsigned short) * 6, .nr = grid * grid, .fatal_fail = 1);

    for (int z = 0; z <= grid; z++)
        for (int x = 0; x <= grid; x++) {
            float *v = &vx[(z * (grid + 1) + x) * 3];

            v[0] = x;
            v[1] = 2.0 * sinf(x * 0.15) * cosf(z * 0.1);
            v[2] = z;
        }

    for (int z = 0; z < grid; z++)
        for (int x = 0; x < grid; x++) {
            unsigned short corner = z * (grid + 1) + x;

            idx[nr_idx++] = corner;
            idx[nr_idx++] = corner + grid + 1;
            idx[nr_idx++] = corner + 1;
            idx[nr_idx++] = corner + 1;
            idx[nr_idx++] = corner + grid + 1;
            idx[nr_idx++] = corner + grid + 2;
        }

    struct mesh *mesh = ref_new(mesh, .name = "grid");
    if (mesh) {
        cerr err = mesh_attr_dup(mesh, MESH_VX, vx, sizeof(float) * 3, nr_vx);
        if (!IS_CERR(err))  err = mesh_attr_dup(mesh, MESH_IDX, idx, sizeof(*idx), nr_idx);
        if (IS_CERR(err)) {
            ref_put(mesh);
            mesh = nullptr;
        }
    }

    mem_free(vx);
    mem_free(idx);

    return mesh;
}

/*
 * The levels are mesh_idx_to_lod()'s, up to the first one that comes out no
 * smaller than the mesh or only sloppily simplified: a 64 x 64 grid keeps
 * all of them, a single quad none
 */
static int mesh_lods_match(int grid, unsigned int expected)
{
    LOCAL_SET(mesh_t, mesh) = grid_mesh(grid);
    if (!mesh)  return EXIT_FAILURE;

    struct mesh_lods lods;
    mesh_lods_build(mesh, &lods);

    int ret = lods.nr == expected ? EXIT_SUCCESS : EXIT_FAILURE;
    for (unsigned int level = 0; level <= lods.nr && level < LOD_MAX - 1; level++) {
        unsigned short *idx;
        float error;
        size_t nr_idx = mesh_idx_to_lod(mesh, level, &idx, &error);

        if (level == lods.nr) {
            /* the first level left out fails the rule */
            if (nr_idx < mesh_nr_idx(mesh) && error >= 0.0) ret = EXIT_FAILURE;
        } else if (nr_idx != lods.nr_idx[level] || error != lods.error[level] ||
                   memcmp(idx, lods.idx[level], nr_idx * sizeof(*idx))) {
            ret = EXIT_FAILURE;
        }

        if (verbose)    msg("level %u: %zu of %zu indices, error %f\n", level, nr_idx, mesh_nr_idx(mesh), error);
        mem_free(idx);
    }

    mesh_lods_done(&lods);

    return ret;
}

static int mesh_lods_match_test(void)
{
    return mesh_lods_match(64, LOD_MAX - 1) || mesh_lods_match(1, 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}

/* Every level is coarser than the mesh and indexes its vertices; done leaves nothing */
static int mesh_lods_bounds_test(void)
{
    LOCAL_SET(mesh_t, mesh) = grid_mesh(64);
    if (!mesh)  return EXIT_FAILURE;

    struct mesh_lods lods;
    mesh_lods_build(mesh, &lods);

    int ret = EXIT_SUCCESS;
    for (unsigned int level = 0; level < lods.nr; level++) {
        if (lods.nr_idx[level] >= mesh_nr_idx(mesh) || lods.nr_idx[level] % 3)  ret = EXIT_FAILURE;

        for (size_t i = 0; i < lods.nr_idx[level]; i++)
            if (lods.idx[level][i] >= mesh_nr_vx(mesh)) ret = EXIT_FAILURE;
    }

    mesh_lods_done(&lods);
    if (lods.nr)    ret = EXIT_FAILURE;
    for (unsigned int level = 0; level < LOD_MAX - 1; level++)
        if (lods.idx[level])    ret = EXIT_FAILURE;

    return ret;
}

static const struct test tests[] = {
    { .name = "mesh attr resize to zero",         .test = mesh_attr_resize_zero_test },
    { .name = "mesh lods match the simplifier",   .test = mesh_lods_match_test },
    { .name = "mesh lods in bounds",              .test = mesh_lods_bounds_test },
};

int main(int argc, char **argv, char **envp)
{
    /* meshoptimizer allocates through the engine's allocator */
    mesh_init();

    return test_main(argc, argv, envp, tests, array_size(tests));
}
