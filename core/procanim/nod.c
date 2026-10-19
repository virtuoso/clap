// SPDX-License-Identifier: Apache-2.0
/*
 * A joint nodding as the entity goes: a head bobbing in motion. The nod's
 * size follows how much the entity goes, and its phase moves on as fast as
 * the size makes it, so the nod quickens as it widens and freezes as it
 * settles; it turns the joint about the model's sideways axis taken into
 * the joint's parent frame through the pose so far, on top of the posed
 * rotation.
 */
#include <math.h>
#include <string.h>
#include "interp.h"
#include "util.h"
#include "procanim/nod.h"

static int nod_joint(const nod_config *cfg, model3d *m)
{
    if (cfg->joint >= 0)    return cfg->joint;

    cres(joint_type) jt = model3d_get_joint(m, cfg->joint_type);
    return IS_CERR(jt) ? -1 : (int)jt.val;
}

static void nod_start(const void *_cfg, void *priv, entity3d *e)
{
    nod_t *n = priv;

    n->size = 0.0f;
    n->phase = 0.0f;
}

static cerr nod_eval(const void *_cfg, void *priv, entity3d *e, double t, double dt,
                     struct joint *out)
{
    const nod_config *cfg = _cfg;
    nod_t *n = priv;
    model3d *m = e->txmodel->model;
    int joint = nod_joint(cfg, m);

    if (joint < 0 || (unsigned int)joint >= m->nr_joints)   return CERR_INVALID_ARGUMENTS;

    float want = clampf(n->amount, 0.0f, 1.0f);
    if (cfg->rate > 0.0f)   n->size = linf_interp(n->size, want, approachf(cfg->rate, (float)dt));
    else                    n->size = want;
    n->phase = fmodf(n->phase + 2.0f * (float)M_PI * cfg->frequency * n->size * (float)dt, 2.0f * (float)M_PI);

    /* the model's sideways axis, in the joint's parent frame as posed so far */
    mat4x4 parent, inv;
    vec4 side = { 1.0f, 0.0f, 0.0f, 0.0f }, axis;

    entity3d_joint_global(e, m->joints[joint].parent, parent);
    mat4x4_invert(inv, parent);
    mat4x4_mul_vec4(axis, inv, side);

    /* out[joint] is read while quat_mul() writes it */
    quat turn, posed;
    memcpy(posed, out[joint].rotation, sizeof(quat));
    quat_from_axis_angle(turn, axis, cfg->angle * n->size * sinf(n->phase));
    quat_mul(out[joint].rotation, turn, posed);

    return CERR_OK;
}

const anim_pose_ops nod_pose_ops = {
    .start  = nod_start,
    .eval   = nod_eval,
};

cerr nod_mask(const nod_config *cfg, animator_t *a, model3d *m, unsigned int layer)
{
    int joint = nod_joint(cfg, m);

    if (joint < 0 || (unsigned int)joint >= m->nr_joints)   return CERR_INVALID_ARGUMENTS;

    return animator_mask_chain(a, layer, joint, 1);
}
