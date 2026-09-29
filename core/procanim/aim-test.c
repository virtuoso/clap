// SPDX-License-Identifier: Apache-2.0
/*
 * Aim point tests, built as the standalone aim_tests binary: it links
 * target.c and the shared test harness, with the one model.c call the
 * point's FOLLOW law makes stubbed out.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "error.h"
#include "interp.h"
#include "linmath.h"
#include "model.h"
#include "procanim/target.h"
#include "util.h"
#include "test.h"

/* the test's entities have no transform: model space is world space */
void entity3d_world_to_model(entity3d *e, const vec3 world, vec3 model)
{
    vec3_dup(model, (float *)world);
}

static bool near3(const vec3 a, float x, float y, float z, float eps)
{
    return fabsf(a[0] - x) < eps && fabsf(a[1] - y) < eps && fabsf(a[2] - z) < eps;
}

static int target_point_test(void)
{
    target_law law = { .kind = TARGET_POINT };
    target_t t = { .point = { 1.0f, 2.0f, 3.0f } };
    vec3 out;

    target_start(&t, &law);
    target_update(&t, &law, nullptr, 0.0, 0.016, out);
    if (!near3(out, 1.0f, 2.0f, 3.0f, 1e-6f))   return EXIT_FAILURE;
    if (target_done(&law, 100.0))               return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int target_follow_test(void)
{
    target_law snap = { .kind = TARGET_FOLLOW }, lp = { .kind = TARGET_FOLLOW, .rate = 4.0 };
    entity3d e = { .aabb_center = { 2.0f, 0.0f, 0.0f } };
    target_t t = { .entity = &e, .offset = { 0.0f, 1.0f, 0.0f } };
    vec3 out;

    /* rate 0 snaps to the entity's center plus the offset */
    target_update(&t, &snap, nullptr, 0.0, 0.016, out);
    if (!near3(out, 2.0f, 1.0f, 0.0f, 1e-6f))   return EXIT_FAILURE;

    /* a rate approaches: strictly between, closing in */
    t.entity = nullptr;
    vec3_dup(t.point, (vec3){ 4.0f, 1.0f, 0.0f });
    target_update(&t, &lp, nullptr, 0.0, 0.1, out);
    float first = out[0];
    if (first <= 2.0f || first >= 4.0f)         return EXIT_FAILURE;
    target_update(&t, &lp, nullptr, 0.1, 0.1, out);
    if (out[0] <= first || out[0] >= 4.0f)                              return EXIT_FAILURE;
    if (fabsf(out[0] - 2.0f - 2.0f * approachf(4.0f, 0.2f)) > 1e-3f)    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int target_move_hold_test(void)
{
    target_law move = { .kind = TARGET_MOVE, .duration = 0.5, .ease = smoothf };
    target_law hold = { .kind = TARGET_HOLD, .duration = 0.25 };
    target_t t = { .cur = { 0.0f, 0.0f, 0.0f }, .point = { 0.0f, 0.0f, 2.0f } };
    vec3 out;

    /* a move anchors on where the point is, eases, arrives, ends at its duration */
    target_start(&t, &move);
    target_update(&t, &move, nullptr, 0.0, 0.016, out);
    if (!near3(out, 0.0f, 0.0f, 0.0f, 1e-6f))   return EXIT_FAILURE;
    target_update(&t, &move, nullptr, 0.25, 0.016, out);
    if (!near3(out, 0.0f, 0.0f, 1.0f, 1e-5f))   return EXIT_FAILURE;
    target_update(&t, &move, nullptr, 0.125, 0.016, out);
    if (out[2] >= 0.5f)                         return EXIT_FAILURE;    /* eased in: behind linear */
    if (target_done(&move, 0.4))                return EXIT_FAILURE;
    target_update(&t, &move, nullptr, 0.7, 0.016, out);
    if (!near3(out, 0.0f, 0.0f, 2.0f, 1e-6f))   return EXIT_FAILURE;    /* clamped past the end */
    if (!target_done(&move, 0.5))               return EXIT_FAILURE;

    /* a hold stays put whatever the point says */
    vec3_dup(t.point, (vec3){ 9.0f, 9.0f, 9.0f });
    target_start(&t, &hold);
    target_update(&t, &hold, nullptr, 0.1, 0.016, out);
    if (!near3(out, 0.0f, 0.0f, 2.0f, 1e-6f))                   return EXIT_FAILURE;
    if (target_done(&hold, 0.2) || !target_done(&hold, 0.25))   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* the angles of a direction, applied, point the forward axis along it; the posed frame counts */
static int aim_round_trip_test(void)
{
    vec3 forward = { 0.0f, 0.0f, 1.0f }, up = { 0.0f, 1.0f, 0.0f };
    quat rest, out;
    double yaw, pitch;

    /* identity pose: right is +X, so +X is a quarter turn of yaw, +Y a quarter turn of pitch */
    quat_identity(rest);
    if (!aim_angles(rest, forward, up, (vec3){ 1.0f, 0.0f, 0.0f }, &yaw, &pitch))   return EXIT_FAILURE;
    if (fabs(yaw - M_PI / 2) > 1e-5 || fabs(pitch) > 1e-5)                          return EXIT_FAILURE;
    aim_angles(rest, forward, up, (vec3){ 0.0f, 1.0f, 0.0f }, &yaw, &pitch);
    if (fabs(yaw) > 1e-5 || fabs(pitch - M_PI / 2) > 1e-5)                          return EXIT_FAILURE;

    /* a zero direction is no direction */
    if (aim_angles(rest, forward, up, (vec3){ 0.0f, 0.0f, 0.0f }, &yaw, &pitch))    return EXIT_FAILURE;

    /* a posed joint, an off-axis direction: the aimed forward lands on it */
    quat_from_axis_angle(rest, (vec3){ 0.3f, 1.0f, -0.2f }, 0.8f);
    vec3 dir = { -1.5f, 0.7f, 2.0f }, aimed;
    aim_angles(rest, forward, up, dir, &yaw, &pitch);
    aim_rotate(out, rest, forward, up, yaw, pitch);
    quat_mul_vec3(aimed, out, forward);
    vec3_norm(dir, dir);
    if (!near3(aimed, dir[0], dir[1], dir[2], 1e-4f))   return EXIT_FAILURE;

    /* zero angles leave the pose alone */
    aim_rotate(out, rest, forward, up, 0.0, 0.0);
    if (fabsf(quat_inner_product(out, rest)) < 1.0f - 1e-6f)    return EXIT_FAILURE;

    /* aiming in place, the way the effectors do, is the same rotation */
    quat in_place;
    memcpy(in_place, rest, sizeof(quat));
    aim_rotate(in_place, in_place, forward, up, yaw, pitch);
    aim_rotate(out, rest, forward, up, yaw, pitch);
    if (fabsf(quat_inner_product(out, in_place)) < 1.0f - 1e-6f)    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "target point",       .test = target_point_test },
    { .name = "target follow",      .test = target_follow_test },
    { .name = "target move, hold",  .test = target_move_hold_test },
    { .name = "aim round trip",     .test = aim_round_trip_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
