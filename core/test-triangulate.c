// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdlib.h>
#include "memory.h"
#include "test.h"
#include "triangulate.h"
#include "util.h"

/*
 * Cross-check a triangulation against the source polygon: every triangle
 * is counterclockwise, indices are within bounds and the total area
 * matches @exp_area
 */
static int tris_check(const vec2 *points, size_t nr_points, const poly_tris *tris,
                      double exp_area)
{
    double area = 0.0;

    for (size_t t = 0; t < tris->nr_tris; t++) {
        const unsigned int *ti = &tris->idx[t * 3];

        for (int k = 0; k < 3; k++)
            if (ti[k] >= nr_points)             return EXIT_FAILURE;

        const float *a = points[ti[0]], *b = points[ti[1]], *c = points[ti[2]];
        double cr = ((double)b[0] - a[0]) * ((double)c[1] - a[1]) -
                    ((double)c[0] - a[0]) * ((double)b[1] - a[1]);
        if (cr <= 0.0)                          return EXIT_FAILURE;

        area += cr / 2.0;
    }

    if (fabs(area - exp_area) > 1e-6)           return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int triangulate_test_square(void)
{
    vec2 points[] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    poly_ring ring = { .start = 0, .nr = 4 };

    cres(poly_tris) res = poly_triangulate(points, &ring, 1);
    if (IS_CERR(res))                           return EXIT_FAILURE;

    int ret = res.val.nr_tris == 2 ?
        tris_check(points, array_size(points), &res.val, 1.0) : EXIT_FAILURE;
    mem_free(res.val.idx);

    return ret;
}

static int triangulate_test_concave(void)
{
    /* L-shape, clockwise on top of it: winding must not matter */
    vec2 points[] = { { 0, 0 }, { 0, 2 }, { 1, 2 }, { 1, 1 }, { 2, 1 }, { 2, 0 } };
    poly_ring ring = { .start = 0, .nr = 6 };

    cres(poly_tris) res = poly_triangulate(points, &ring, 1);
    if (IS_CERR(res))                           return EXIT_FAILURE;

    int ret = res.val.nr_tris == 4 ?
        tris_check(points, array_size(points), &res.val, 3.0) : EXIT_FAILURE;
    mem_free(res.val.idx);

    return ret;
}

static int triangulate_test_hole(void)
{
    vec2 points[] = {
        { 0, 0 }, { 4, 0 }, { 4, 4 }, { 0, 4 },     /* outer, counterclockwise */
        { 1, 1 }, { 3, 1 }, { 3, 3 }, { 1, 3 },     /* hole, also counterclockwise */
    };
    poly_ring rings[] = {
        { .start = 0, .nr = 4 },
        { .start = 4, .nr = 4 },
    };

    bool hole[array_size(rings)];
    cerr err = poly_rings_classify(points, rings, array_size(rings), hole);
    if (IS_CERR(err))                           return EXIT_FAILURE;
    if (hole[0] || !hole[1])                    return EXIT_FAILURE;

    cres(poly_tris) res = poly_triangulate(points, rings, array_size(rings));
    if (IS_CERR(res))                           return EXIT_FAILURE;

    /* bridged ring has 10 vertices => exactly 8 triangles */
    int ret = res.val.nr_tris == 8 ?
        tris_check(points, array_size(points), &res.val, 16.0 - 4.0) : EXIT_FAILURE;
    mem_free(res.val.idx);

    return ret;
}

static int triangulate_test_multi(void)
{
    /* two disjoint fill rings, one with a hole with an island inside it */
    vec2 points[] = {
        { 0, 0 }, { 6, 0 }, { 6, 6 }, { 0, 6 },     /* outer */
        { 1, 1 }, { 1, 5 }, { 5, 5 }, { 5, 1 },     /* hole in it, clockwise */
        { 2, 2 }, { 4, 2 }, { 4, 4 }, { 2, 4 },     /* island inside the hole */
        { 7, 0 }, { 9, 0 }, { 8, 2 },               /* separate triangle */
    };
    poly_ring rings[] = {
        { .start = 0,  .nr = 4 },
        { .start = 4,  .nr = 4 },
        { .start = 8,  .nr = 4 },
        { .start = 12, .nr = 3 },
    };

    bool hole[array_size(rings)];
    cerr err = poly_rings_classify(points, rings, array_size(rings), hole);
    if (IS_CERR(err))                               return EXIT_FAILURE;
    if (hole[0] || !hole[1] || hole[2] || hole[3])  return EXIT_FAILURE;

    cres(poly_tris) res = poly_triangulate(points, rings, array_size(rings));
    if (IS_CERR(res))                           return EXIT_FAILURE;

    double exp_area = 36.0 - 16.0 + 4.0 + 2.0;
    int ret = tris_check(points, array_size(points), &res.val, exp_area);
    mem_free(res.val.idx);

    return ret;
}

static int triangulate_test_degenerate(void)
{
    vec2 points[] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    poly_ring ring = { .start = 0, .nr = 2 };

    cres(poly_tris) res = poly_triangulate(nullptr, &ring, 1);
    if (!IS_CERR(res))                          return EXIT_FAILURE;
    res = poly_triangulate(points, nullptr, 1);
    if (!IS_CERR(res))                          return EXIT_FAILURE;
    res = poly_triangulate(points, &ring, 0);
    if (!IS_CERR(res))                          return EXIT_FAILURE;

    /* a 2-point ring is skipped, leaving nothing to triangulate */
    res = poly_triangulate(points, &ring, 1);
    if (IS_CERR(res))                           return EXIT_FAILURE;

    int ret = !res.val.nr_tris && !res.val.idx ? EXIT_SUCCESS : EXIT_FAILURE;
    mem_free(res.val.idx);

    return ret;
}

static const struct test tests[] = {
    { .name = "triangulate square", .test = triangulate_test_square },
    { .name = "triangulate concave", .test = triangulate_test_concave },
    { .name = "triangulate hole", .test = triangulate_test_hole },
    { .name = "triangulate multi-ring", .test = triangulate_test_multi },
    { .name = "triangulate degenerate", .test = triangulate_test_degenerate },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
