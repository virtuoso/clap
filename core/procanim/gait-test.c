// SPDX-License-Identifier: Apache-2.0
/*
 * Procedural-animation (gait) tests, built as the standalone procanim_tests
 * binary: it links only gait.c (pure math) and the shared test harness
 * (test_main() in core/test-harness.c), not the whole engine test blob.
 */
#include <math.h>
#include <stdlib.h>
#include "error.h"
#include "linmath.h"
#include "procanim/gait.h"
#include "util.h"
#include "test.h"

static int gait_walk_test(void)
{
    gait_leg legs[2] = {
        { .offset = { -0.28f, 0.0f, 0.0f }, .phase = 0.0f },
        { .offset = {  0.28f, 0.0f, 0.0f }, .phase = 0.5f },
    };
    gait g = {
        .legs = legs, .nr_legs = 2,
        .cycle = 1.0f, .swing = 0.38f, .stride = 0.5f, .step_height = 0.15f, .foot_y = 0.1f,
    };
    vec3 fwd = { 0, 0, 1 }, right = { 1, 0, 0 }, pos = { 0, 0, 0 };
    float speed = g.stride / g.cycle;

    gait_reset(&g, pos, fwd, right);
    if (legs[0].target[0] != -0.28f || legs[0].target[1] != 0.1f)   return EXIT_FAILURE;

    vec3 prev[2];
    bool prev_swing[2] = {}, saw_swing[2] = {}, saw_plant[2] = {};
    float plant_z[2] = { HUGE_VALF, HUGE_VALF };

    vec3_dup(prev[0], legs[0].target);
    vec3_dup(prev[1], legs[1].target);

    for (double t = 0.0; t < 3.0; t += 1.0 / 120.0) {
        pos[2] = speed * (float)t;
        cerr err = gait_update(&g, pos, fwd, right, speed, 1.0f / 120.0f);
        if (IS_CERR(err))                       return EXIT_FAILURE;

        /* alternating gait: never both feet airborne */
        if (legs[0].swinging && legs[1].swinging)   return EXIT_FAILURE;

        for (int i = 0; i < 2; i++) {
            gait_leg *leg = &legs[i];

            /* planted feet stay put and on the ground */
            if (!leg->swinging && !prev_swing[i]) {
                vec3 d;

                vec3_sub(d, leg->target, prev[i]);
                if (vec3_len(d) > 1e-6f)        return EXIT_FAILURE;
                if (leg->target[1] != g.foot_y) return EXIT_FAILURE;
            }

            /* swing stays within the arc and on its side */
            if (leg->target[1] < g.foot_y - 1e-4f ||
                leg->target[1] > g.foot_y + g.step_height + 1e-4f)
                return EXIT_FAILURE;
            if (fabsf(leg->target[0] - leg->offset[0]) > 1e-4f) return EXIT_FAILURE;

            /* successive footfalls of one leg are a stride apart */
            if (prev_swing[i] && !leg->swinging) {
                saw_plant[i] = true;
                if (plant_z[i] < HUGE_VALF &&
                    fabsf(leg->target[2] - plant_z[i] - g.stride) > 0.02f)
                    return EXIT_FAILURE;
                plant_z[i] = leg->target[2];
            }

            saw_swing[i] |= leg->swinging;
            prev_swing[i] = leg->swinging;
            vec3_dup(prev[i], leg->target);
        }
    }

    if (!saw_swing[0] || !saw_swing[1] || !saw_plant[0] || !saw_plant[1])   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int gait_run_test(void)
{
    gait_leg legs[2] = {
        { .offset = { -0.28f, 0.0f, 0.0f }, .phase = 0.0f },
        { .offset = {  0.28f, 0.0f, 0.0f }, .phase = 0.5f },
    };
    gait g = {
        .legs = legs, .nr_legs = 2,
        .cycle = 0.7f, .swing = 0.6f, .stride = 0.9f, .step_height = 0.25f, .foot_y = 0.1f,
    };
    vec3 fwd = { 0, 0, 1 }, right = { 1, 0, 0 }, pos = { 0, 0, 0 };
    float speed = g.stride / g.cycle;
    bool prev_swing[2] = {}, saw_flight = false;
    vec3 prev[2];

    gait_reset(&g, pos, fwd, right);
    vec3_dup(prev[0], legs[0].target);
    vec3_dup(prev[1], legs[1].target);

    for (double t = 0.0; t < 3.0; t += 1.0 / 120.0) {
        pos[2] = speed * (float)t;
        cerr err = gait_update(&g, pos, fwd, right, speed, 1.0f / 120.0f);
        if (IS_CERR(err))                       return EXIT_FAILURE;

        /* running has flight phases: both feet airborne at once */
        saw_flight |= legs[0].swinging && legs[1].swinging;

        /* planted feet still never move */
        for (int i = 0; i < 2; i++) {
            if (!legs[i].swinging && !prev_swing[i]) {
                vec3 d;

                vec3_sub(d, legs[i].target, prev[i]);
                if (vec3_len(d) > 1e-6f)        return EXIT_FAILURE;
            }
            prev_swing[i] = legs[i].swinging;
            vec3_dup(prev[i], legs[i].target);
        }
    }

    return saw_flight ? EXIT_SUCCESS : EXIT_FAILURE;
}

static const struct test tests[] = {
    { .name = "gait walk",        .test = gait_walk_test },
    { .name = "gait run flight",  .test = gait_run_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
