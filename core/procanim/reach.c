// SPDX-License-Identifier: Apache-2.0
/*
 * An IK chain's tip at a point: a tentacle's tip on its prey, a hand on a
 * handle. The aim point does the motion (follow, move, hold), the chain
 * does the posing.
 */
#include "model.h"
#include "procanim/reach.h"

static void reach_start(const void *_cfg, void *priv, entity3d *e)
{
    const reach_config *cfg = _cfg;
    reach_t *r = priv;

    target_start(&r->target, &cfg->law);
}

static cerr reach_eval(const void *_cfg, void *priv, entity3d *e, double t, double dt,
                       struct joint *out)
{
    const reach_config *cfg = _cfg;
    reach_t *r = priv;

    if (!r->chain)          return CERR_INVALID_ARGUMENTS;
    if (out != e->joints)   return CERR_NOT_SUPPORTED;

    vec3 point;

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

    return ik_chain_solve(r->chain,
                          .target = { point[0], point[1], point[2] },
                          .pole   = { cfg->pole[0], cfg->pole[1], cfg->pole[2] },
                          .pinned = pinned,
                          .pose   = pose);
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
