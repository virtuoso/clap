// SPDX-License-Identifier: Apache-2.0
/*
 * Walking gait: phase-windowed stepping over world-planted anchors.
 * Positions only - the caller owns the body and the IK; this module
 * decides when each foot lifts and where it lands.
 */
#include <math.h>
#include "interp.h"
#include "gait.h"

#define GAIT_CYCLE  1.1f
#define GAIT_SWING  0.38f
#define GAIT_STRIDE 0.55f
#define GAIT_STEP_H 0.16f

/* planted foot height at (x, z) */
static float gait_ground(gait *g, float x, float z)
{
    return (g->ground ? g->ground(g->ground_priv, x, z) : 0.0f) + g->foot_y;
}

void gait_reset(gait *g, const vec3 pos, const vec3 forward, const vec3 right)
{
    g->phase = 0.0f;

    for (size_t i = 0; i < g->nr_legs; i++) {
        gait_leg *leg = &g->legs[i];

        for (int c = 0; c < 3; c++) leg->target[c] = pos[c] + forward[c] * leg->offset[2] + right[c] * leg->offset[0];
        leg->target[1] = gait_ground(g, leg->target[0], leg->target[2]);
        leg->swinging = false;
    }
}

cerr gait_update(gait *g, const vec3 pos, const vec3 forward, const vec3 right,
                 float speed, float dt)
{
    if (!g || !g->legs || !g->nr_legs)  return CERR_INVALID_ARGUMENTS;

    float cycle = g->cycle ? : GAIT_CYCLE;
    float swing = g->swing ? : GAIT_SWING;
    float stride = g->stride ? : GAIT_STRIDE;
    float step_h = g->step_height ? : GAIT_STEP_H;

    g->phase += dt / cycle;
    g->phase -= floorf(g->phase);

    for (size_t i = 0; i < g->nr_legs; i++) {
        gait_leg *leg = &g->legs[i];
        float u = g->phase - leg->phase;

        u -= floorf(u);

        if (u >= swing) {
            /* touch down exactly on the predicted landing spot */
            if (leg->swinging)  vec3_dup(leg->target, leg->swing_to);
            leg->swinging = false;
            continue;
        }

        if (!leg->swinging) {
            leg->swinging = true;
            vec3_dup(leg->swing_from, leg->target);

            /* land half a stride ahead of where the body will be */
            float land_time = (swing - u) * cycle;
            float lead = speed * land_time + stride / 2.0f + leg->offset[2];

            for (int c = 0; c < 3; c++) leg->swing_to[c] = pos[c] + forward[c] * lead + right[c] * leg->offset[0];
            leg->swing_to[1] = gait_ground(g, leg->swing_to[0], leg->swing_to[2]);
        }

        float su = u / swing;

        vec3_interp(leg->target, leg->swing_from, leg->swing_to, smoothf(su));
        leg->target[1] += step_h * sinf((float)M_PI * su);
    }

    return CERR_OK;
}
