// SPDX-License-Identifier: Apache-2.0
/*
 * FABRIK inverse kinematics: Forward And Backward Reaching IK after
 * Aristidou & Lasenby. Joint positions are pulled onto their segments
 * alternately from the target end and from the root end until the end
 * effector converges onto the target.
 */
#include <float.h>
#include <math.h>
#include <sys/types.h>
#include "common.h"
#include "fabrik.h"

#define FABRIK_MAX_ITERATIONS   16
/* default tolerance as a fraction of the total chain length */
#define FABRIK_TOLERANCE        1e-4f

static float dist3(const vec3 a, const vec3 b)
{
    vec3 d;
    vec3_sub(d, b, a);

    return vec3_len(d);
}

/*
 * Direction from @from to @to; when the points coincide, fall back to
 * @fallback, which the caller keeps unit length
 */
static void seg_dir(vec3 dir, const vec3 from, const vec3 to, const vec3 fallback)
{
    vec3_sub(dir, to, from);

    float len = vec3_len(dir);
    if (len < FLT_EPSILON) {
        vec3_dup(dir, fallback);
        return;
    }

    vec3_scale(dir, dir, 1.0f / len);
}

/*
 * Seed the chain as a circular arc whose chord is the root-to-target
 * segment and whose length is the chain's, bowed toward the pole. The
 * seed picks the fold side: without it, a straight chain reaching for a
 * target on its own axis folds degenerately. It has to be built from
 * scratch rather than derived from the input configuration, which picks
 * the wrong side whenever it lies off the root-to-target line (a leg
 * pointing down, the target ahead); and it has to respect the segment
 * lengths, or a slack chain seeded onto the chord alone (a tentacle
 * reaching halfway) starts as a compressed accordion that the passes
 * crumple into knots.
 */
static void pole_seed(const fabrik_solve_opts *opts, const vec3 reach_dir, float total, float dist)
{
    vec3 side;

    vec3_dup(side, opts->pole);

    float along = vec3_mul_inner(side, reach_dir);
    vec3 tangent;
    vec3_scale(tangent, reach_dir, along);
    vec3_sub(side, side, tangent);

    float len = vec3_len(side);
    if (len < FLT_EPSILON)  return;

    vec3_scale(side, side, 1.0f / len);

    /* arc half-angle from chord / arc-length: sin(a) / a = dist / total */
    float ratio = dist / total;
    float lo = 1e-4f, hi = (float)M_PI - 1e-4f, a = 0.0f;
    for (int it = 0; it < 32; it++) {
        a = (lo + hi) / 2.0f;
        if (sinf(a) / a > ratio)    lo = a;
        else                        hi = a;
    }

    float radius = dist / (2.0f * sinf(a));

    float walked = 0.0f;
    for (size_t i = 1; i < opts->nr - 1; i++) {
        walked += opts->lengths[i - 1];
        float phi = 2.0f * a * (walked / total) - a;

        vec3_add_scaled(opts->points[i], opts->points[0], reach_dir, 1.0f,
                        dist / 2.0f + radius * sinf(phi));
        vec3_add_scaled(opts->points[i], opts->points[i], side, 1.0f,
                        radius * (cosf(phi) - cosf(a)));
    }
}

/*
 * Two segments and a pole have a closed form: the middle point sits on
 * the circle where the two segments' spheres meet, on the pole's side of
 * the root-to-target line. Exact and the same for the same input, where
 * the passes below stop anywhere within the tolerance and crawl on a
 * tightly folded chain: a leg's knee does not jitter with its foot.
 * A target closer than the segments' difference is out of reach inward:
 * the end lands on the line as near as it gets. False for a pole along
 * the line or a degenerate triangle, which the passes handle.
 */
static bool two_bone(const fabrik_solve_opts *opts, const vec3 root, const vec3 reach_dir, float dist)
{
    float l0 = opts->lengths[0], l1 = opts->lengths[1];
    vec3 side, along;

    vec3_scale(along, reach_dir, vec3_mul_inner(opts->pole, reach_dir));
    vec3_sub(side, opts->pole, along);

    float len = vec3_len(side);
    if (len < FLT_EPSILON)  return false;
    vec3_scale(side, side, 1.0f / len);

    float d = fmaxf(dist, fabsf(l0 - l1));
    if (d < FLT_EPSILON)    return false;

    /* the foot of the middle point on the line, and its height over it */
    float a = (l0 * l0 - l1 * l1 + d * d) / (2.0f * d);
    float h = sqrtf(fmaxf(l0 * l0 - a * a, 0.0f));

    vec3_add_scaled(opts->points[1], root, reach_dir, 1.0f, a);
    vec3_add_scaled(opts->points[1], opts->points[1], side, 1.0f, h);
    vec3_add_scaled(opts->points[2], root, reach_dir, 1.0f, d);

    return true;
}

cerr _fabrik_solve(const fabrik_solve_opts *opts)
{
    vec3 *p = opts->points;
    size_t nr = opts->nr;

    if (!p || !opts->lengths || nr < 2) return CERR_INVALID_ARGUMENTS;

    float total = 0.0f;
    for (size_t i = 0; i < nr - 1; i++) {
        if (opts->lengths[i] <= 0.0f)   return CERR_INVALID_ARGUMENTS;
        total += opts->lengths[i];
    }

    vec3 root, reach_dir;
    vec3_dup(root, p[0]);
    seg_dir(reach_dir, root, opts->target, (vec3){ 0.0f, 1.0f, 0.0f });

    float dist = dist3(root, opts->target);

    /* out of reach: lay the chain straight toward the target */
    if (dist >= total) {
        for (size_t i = 0; i < nr - 1; i++) vec3_add_scaled(p[i + 1], p[i], reach_dir, 1.0f, opts->lengths[i]);
        return CERR_OK;
    }

    if (vec3_len(opts->pole) > 0.0f) {
        if (nr == 3 && two_bone(opts, root, reach_dir, dist))   return CERR_OK;
        pole_seed(opts, reach_dir, total, dist);
    }

    unsigned int max_iterations = opts->max_iterations ? : FABRIK_MAX_ITERATIONS;
    float tolerance = opts->tolerance ? : FABRIK_TOLERANCE * total;

    for (unsigned int iter = 0; iter < max_iterations; iter++) {
        /* backward: pin the end effector, follow toward the root */
        vec3_dup(p[nr - 1], opts->target);
        for (ssize_t i = nr - 2; i >= 0; i--) {
            vec3 dir;
            seg_dir(dir, p[i + 1], p[i], reach_dir);
            vec3_add_scaled(p[i], p[i + 1], dir, 1.0f, opts->lengths[i]);
        }

        /* forward: pin the root, follow toward the end effector */
        vec3_dup(p[0], root);
        for (size_t i = 0; i < nr - 1; i++) {
            vec3 dir;
            seg_dir(dir, p[i], p[i + 1], reach_dir);
            vec3_add_scaled(p[i + 1], p[i], dir, 1.0f, opts->lengths[i]);
        }

        if (dist3(p[nr - 1], opts->target) <= tolerance)    break;
    }

    return CERR_OK;
}
