// SPDX-License-Identifier: Apache-2.0
/*
 * Inverse-kinematics tests, built as the standalone ik_tests binary: it links
 * only fabrik.c (pure math) and the shared test harness (test_main() in
 * core/test-harness.c), not the whole engine test blob.
 */
#include <math.h>
#include <stdlib.h>
#include "common.h"
#include "error.h"
#include "linmath.h"
#include "fabrik.h"
#include "util.h"
#include "test.h"

/*
 * Cross-check a solved chain: the root didn't move from @root, every
 * segment kept its rest length and the end effector is within @tolerance
 * of @target
 */
static int chain_check(const vec3 *points, const float *lengths, size_t nr,
                       const vec3 root, const vec3 target, float tolerance)
{
    vec3 d;

    vec3_sub(d, points[0], root);
    if (vec3_len(d) > 1e-6f)                    return EXIT_FAILURE;

    for (size_t i = 0; i < nr - 1; i++) {
        vec3_sub(d, points[i + 1], points[i]);
        if (fabsf(vec3_len(d) - lengths[i]) > 1e-3f)    return EXIT_FAILURE;
    }

    vec3_sub(d, points[nr - 1], target);
    if (vec3_len(d) > tolerance)                return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int fabrik_test_reach(void)
{
    vec3 points[] = { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 2, 0 }, { 0, 3, 0 } };
    float lengths[] = { 1, 1, 1 };

    cerr err = fabrik_solve(
        .points     = points,
        .lengths    = lengths,
        .nr         = array_size(points),
        .target     = { 1.5, 1.5, 0 },
        .pole       = { 0, 0, 1 },
        .tolerance  = 1e-4
    );
    if (IS_CERR(err))                           return EXIT_FAILURE;

    return chain_check(points, lengths, array_size(points),
                       (vec3){ 0, 0, 0 }, (vec3){ 1.5, 1.5, 0 }, 1e-4f);
}

static int fabrik_test_unreachable(void)
{
    vec3 points[] = { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 2, 0 }, { 0, 3, 0 } };
    float lengths[] = { 1, 1, 1 };

    cerr err = fabrik_solve(
        .points     = points,
        .lengths    = lengths,
        .nr         = array_size(points),
        .target     = { 8, 0, 0 }
    );
    if (IS_CERR(err))                           return EXIT_FAILURE;

    /* out of reach: the chain lies straight toward the target */
    return chain_check(points, lengths, array_size(points),
                       (vec3){ 0, 0, 0 }, (vec3){ 3, 0, 0 }, 1e-5f);
}

static int fabrik_test_pole(void)
{
    float lengths[] = { 1, 1, 1 };
    float side[] = { 1.0f, -1.0f };

    /* target on the chain's own axis: the pole picks the fold side */
    for (size_t s = 0; s < array_size(side); s++) {
        vec3 points[] = { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 2, 0 }, { 0, 3, 0 } };

        cerr err = fabrik_solve(
            .points     = points,
            .lengths    = lengths,
            .nr         = array_size(points),
            .target     = { 0, 1, 0 },
            .pole       = { 0, 0, side[s] },
            .tolerance  = 1e-4
        );
        if (IS_CERR(err))                       return EXIT_FAILURE;

        if (chain_check(points, lengths, array_size(points),
                        (vec3){ 0, 0, 0 }, (vec3){ 0, 1, 0 }, 1e-4f))
            return EXIT_FAILURE;

        for (size_t i = 1; i < array_size(points) - 1; i++)
            if (points[i][2] * side[s] < 1e-2f) return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int fabrik_test_fold_side(void)
{
    /*
     * The rest chain lies far off the root-to-target line (a leg
     * pointing down with the target ahead): the fold must still pick
     * the pole side, not the side the rest points happened to be on
     */
    vec3 points[] = { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 2, 0 }, { 0, 3, 0 } };
    float lengths[] = { 1, 1, 1 };
    vec3 target = { 0, 2, 1.5 };

    cerr err = fabrik_solve(
        .points     = points,
        .lengths    = lengths,
        .nr         = array_size(points),
        .target     = { 0, 2, 1.5 },
        .pole       = { 0, 0, 1 },
        .tolerance  = 1e-4
    );
    if (IS_CERR(err))                           return EXIT_FAILURE;

    if (chain_check(points, lengths, array_size(points),
                    (vec3){ 0, 0, 0 }, target, 1e-4f))
        return EXIT_FAILURE;

    vec3 reach, perp;
    vec3_norm(reach, target);
    vec3_add_scaled(perp, (vec3){ 0, 0, 1 }, reach, 1.0f, -vec3_mul_inner(reach, (vec3){ 0, 0, 1 }));

    for (size_t i = 1; i < array_size(points) - 1; i++) {
        vec3 bend;

        vec3_dup(bend, points[i]);
        vec3_add_scaled(bend, bend, reach, 1.0f, -vec3_mul_inner(bend, reach));
        if (vec3_mul_inner(bend, perp) < 1e-2f) return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int fabrik_test_slack_smooth(void)
{
    /*
     * A slack chain (target at less than half reach) must come out a
     * smooth bow, not an accordion: consecutive segments stay within
     * 60 degrees of each other
     */
    vec3 points[9];
    float lengths[8];

    for (size_t i = 0; i < array_size(points); i++) {
        points[i][0] = 0;
        points[i][1] = i;
        points[i][2] = 0;
    }
    for (size_t i = 0; i < array_size(lengths); i++)    lengths[i] = 1;

    vec3 target = { 3, 1, 0 };
    cerr err = fabrik_solve(
        .points     = points,
        .lengths    = lengths,
        .nr         = array_size(points),
        .target     = { 3, 1, 0 },
        .pole       = { 0, 1, 0 },
        .tolerance  = 1e-4
    );
    if (IS_CERR(err))                           return EXIT_FAILURE;

    if (chain_check(points, lengths, array_size(points),
                    (vec3){ 0, 0, 0 }, target, 1e-4f))
        return EXIT_FAILURE;

    for (size_t i = 0; i + 2 < array_size(points); i++) {
        vec3 a, b;

        vec3_sub(a, points[i + 1], points[i]);
        vec3_sub(b, points[i + 2], points[i + 1]);
        vec3_norm(a, a);
        vec3_norm(b, b);
        if (vec3_mul_inner(a, b) < 0.5f)        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int fabrik_test_invalid(void)
{
    vec3 points[] = { { 0, 0, 0 }, { 0, 1, 0 } };
    float lengths[] = { 1 };
    float bad_lengths[] = { -1 };

    cerr err = fabrik_solve(.points = nullptr, .lengths = lengths, .nr = 2);
    if (!IS_CERR(err))                          return EXIT_FAILURE;
    err = fabrik_solve(.points = points, .lengths = nullptr, .nr = 2);
    if (!IS_CERR(err))                          return EXIT_FAILURE;
    err = fabrik_solve(.points = points, .lengths = lengths, .nr = 1);
    if (!IS_CERR(err))                          return EXIT_FAILURE;
    err = fabrik_solve(.points = points, .lengths = bad_lengths, .nr = 2);
    if (!IS_CERR(err))                          return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "fabrik reach",        .test = fabrik_test_reach },
    { .name = "fabrik unreachable",  .test = fabrik_test_unreachable },
    { .name = "fabrik pole",         .test = fabrik_test_pole },
    { .name = "fabrik fold side",    .test = fabrik_test_fold_side },
    { .name = "fabrik slack smooth", .test = fabrik_test_slack_smooth },
    { .name = "fabrik invalid",      .test = fabrik_test_invalid },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
