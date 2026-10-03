// SPDX-License-Identifier: Apache-2.0
/*
 * An IK chain's tip at a point: a tentacle's tip on its prey, a hand on a
 * handle. The aim point does the motion (follow, move, hold), the chain
 * does the posing.
 */
#include "model.h"
#include "physics.h"
#include "animator/blend.h"
#include "procanim/reach.h"

/* the ground probe under a planted foot: from this far above it, this far down */
#define REACH_PLANT_ABOVE   0.5f
#define REACH_PLANT_DEPTH   1.5f

/*
 * The planted point: where the pose so far puts the tip, with the ground
 * under it added to its height over the pose's floor (the model's origin
 * plane). No body to probe with, or nothing under it: the pose's point.
 */
static void reach_plant(reach_t *r, entity3d *e, const struct joint *pose, vec3 point)
{
    ik_chain_point(r->chain, pose, r->chain->nr - 1, point);
    if (!e->phys_body)  return;

    vec4 tip = { point[0], point[1], point[2], 1.0f }, world;
    mat4x4_mul_vec4(world, e->mx, tip);

    vec3 start = { world[0], world[1] + REACH_PLANT_ABOVE, world[2] }, down = { 0.0f, -1.0f, 0.0f };
    double dist = REACH_PLANT_ABOVE + REACH_PLANT_DEPTH;

    if (!phys_ray_cast(e, start, down, &dist))  return;

    vec3 ground_world = { start[0], start[1] - (float)dist, start[2] }, ground;

    entity3d_world_to_model(e, ground_world, ground);
    point[1] += ground[1];
}

static void reach_start(const void *_cfg, void *priv, entity3d *e)
{
    const reach_config *cfg = _cfg;
    reach_t *r = priv;

    target_start(&r->target, &cfg->law);
}

static cerr reach_solve(reach_t *r, const reach_config *cfg, const vec3 point, int pinned,
                        const struct joint *pose, struct joint *out)
{
    return ik_chain_solve(r->chain,
                          .target    = { point[0], point[1], point[2] },
                          .pole      = { cfg->pole[0], cfg->pole[1], cfg->pole[2] },
                          .tolerance = cfg->tolerance,
                          .max_iterations = cfg->max_iterations,
                          .pinned    = pinned,
                          .pose      = pose,
                          .out       = out);
}

static cerr reach_eval(const void *_cfg, void *priv, entity3d *e, double t, double dt,
                       struct joint *out)
{
    const reach_config *cfg = _cfg;
    reach_t *r = priv;

    if (!r->chain)          return CERR_INVALID_ARGUMENTS;

    /* an overlay solves over the pose so far, from it, into its own buffer */
    if (out != e->joints)   CERR_RET_CERR(ik_chain_rest_from_pose(r->chain, out));

    vec3 point;

    if (cfg->law.kind == TARGET_PLANT)  reach_plant(r, e, out, r->target.point);
    target_update(&r->target, &cfg->law, e, t, dt, point);

    /* the pin furthest along the chain holds everything up to it */
    animator_t *a = entity3d_animator(e);
    const struct joint *pose = animator_pinned_pose(a);
    int pinned = 0;

    for (int k = (int)r->chain->nr - 2; pose && k > 0; k--)
        if (animator_is_pinned(a, r->chain->joints[k])) {
            pinned = k;
            break;
        }

    double held = animator_pin_weight(a);

    if (!pinned || held >= 1.0) return reach_solve(r, cfg, point, pinned, pose, out);

    /*
     * The pins are letting go: what they hold the chain to, then what the
     * chain does free, blended by how much of the hold is left
     */
    ik_chain *c = r->chain;
    struct joint pinned_pose[c->nr];

    CERR_RET_CERR(reach_solve(r, cfg, point, pinned, pose, out));
    for (size_t k = 0; k < c->nr; k++)  pinned_pose[k] = out[c->joints[k]];

    CERR_RET_CERR(reach_solve(r, cfg, point, 0, nullptr, out));
    for (size_t k = 0; k < c->nr; k++)
        joint_blend(&out[c->joints[k]], &out[c->joints[k]], &pinned_pose[k], (float)held);

    return CERR_OK;
}

static bool reach_done(const void *_cfg, void *priv, double t)
{
    const reach_config *cfg = _cfg;

    return target_done(&cfg->law, t);
}

const anim_pose_ops reach_pose_ops = {
    .start  = reach_start,
    .eval   = reach_eval,
    .done   = reach_done,
};
