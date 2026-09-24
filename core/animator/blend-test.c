// SPDX-License-Identifier: Apache-2.0
/*
 * Joint blend tests, built as the standalone anim_blend_tests binary: it
 * links only blend.c (pure arithmetic) and the shared test harness.
 */
#include <math.h>
#include <stdlib.h>
#include "model.h"
#include "util.h"
#include "test.h"
#include "blend.h"

static bool close_to(float a, float b)
{
    return fabsf(a - b) < 1e-5f;
}

static struct joint joint_at(float tx, float angle_y)
{
    struct joint j = { .translation = { tx, 0.0f, 0.0f }, .scale = { 1.0f, 1.0f, 1.0f } };

    quat_from_axis_angle(j.rotation, (vec3){ 0.0f, 1.0f, 0.0f }, angle_y);
    j.off[0] = 7;   /* a cursor, to see it survive */

    return j;
}

/* w = 0 is @a, w = 1 is @b, the cursors stay the destination's */
static int blend_test_ends(void)
{
    struct joint a = joint_at(0.0f, 0.0f), b = joint_at(2.0f, 1.0f), d = joint_at(9.0f, 0.5f);

    d.off[0] = 3;
    joint_blend(&d, &a, &b, 0.0f);
    if (!close_to(d.translation[0], 0.0f) || !close_to(d.rotation[3], 1.0f) || d.off[0] != 3)   return EXIT_FAILURE;

    joint_blend(&d, &a, &b, 1.0f);
    if (!close_to(d.translation[0], 2.0f) || !close_to(d.rotation[1], sinf(0.5f)))              return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* halfway: the translation is the midpoint, the rotation half the angle, unit length */
static int blend_test_middle(void)
{
    struct joint a = joint_at(0.0f, 0.0f), b = joint_at(2.0f, 1.0f);

    joint_blend(&a, &a, &b, 0.5f);
    if (!close_to(a.translation[0], 1.0f))                                  return EXIT_FAILURE;
    if (!close_to(vec4_len(a.rotation), 1.0f))                              return EXIT_FAILURE;
    if (fabsf(2.0f * atan2f(a.rotation[1], a.rotation[3]) - 0.5f) > 1e-3f)  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* the same rotation with the opposite sign blends to itself, not through zero */
static int blend_test_short_arc(void)
{
    struct joint a = joint_at(0.0f, 1.0f), b = a, d;

    vec4_scale(b.rotation, b.rotation, -1.0f);
    joint_blend(&d, &a, &b, 0.5f);
    if (!close_to(vec4_len(d.rotation), 1.0f))                                  return EXIT_FAILURE;
    if (!close_to(fabsf(vec4_mul_inner(d.rotation, a.rotation)), 1.0f))         return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* the copy takes the transforms and leaves the cursors */
static int blend_test_copy(void)
{
    struct joint src[2] = { joint_at(1.0f, 0.2f), joint_at(3.0f, 0.4f) }, dst[2] = {};

    dst[1].off[1] = 5;
    joints_copy_trs(dst, src, 2);
    if (!close_to(dst[1].translation[0], 3.0f) || !close_to(dst[0].rotation[1], sinf(0.1f)))    return EXIT_FAILURE;
    if (dst[1].off[1] != 5 || dst[0].off[0] != 0)                                               return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "blend ends",         .test = blend_test_ends },
    { .name = "blend middle",       .test = blend_test_middle },
    { .name = "blend short arc",    .test = blend_test_short_arc },
    { .name = "blend copy",         .test = blend_test_copy },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
