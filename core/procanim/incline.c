// SPDX-License-Identifier: Apache-2.0
/*
 * A joint tilted with the ground: a wheeled body onto the ramp it drives
 * up. The up eases toward the normal in world space, so the character's
 * own turning doesn't swing it; then the world's up and the eased one go
 * into the joint's parent frame through the entity's matrix and the pose
 * so far, and the rotation from the one to the other goes on top of the
 * posed rotation.
 */
#include <math.h>
#include <string.h>
#include "interp.h"
#include "model.h"
#include "procanim/incline.h"

/* the configured joint, or the skeleton's root: joint 0's topmost ancestor */
static int incline_joint(const incline_config *cfg, model3d *m)
{
    if (cfg->joint >= 0)    return cfg->joint;
    if (!m->nr_joints)      return -1;

    int joint = 0;
    while (m->joints[joint].parent >= 0)    joint = m->joints[joint].parent;

    return joint;
}

/* A world direction in the frame of @joint's parent as posed so far, unit length */
static void incline_dir_in_parent(entity3d *e, int joint, const vec3 dir, vec3 local)
{
    vec4 world = { dir[0], dir[1], dir[2], 0.0f }, model, parent;
    mat4x4 global, inv;

    mat4x4_mul_vec4(model, e->inverse_mx, world);
    entity3d_joint_global(e, e->txmodel->model->joints[joint].parent, global);
    mat4x4_invert(inv, global);
    mat4x4_mul_vec4(parent, inv, model);
    vec3_norm(local, parent);
}

static void incline_start(const void *_cfg, void *priv, entity3d *e)
{
    incline_t *in = priv;

    vec3_dup(in->up, (vec3){ 0.0f, 1.0f, 0.0f });
}

static cerr incline_eval(const void *_cfg, void *priv, entity3d *e, double t, double dt,
                         struct joint *out)
{
    const incline_config *cfg = _cfg;
    incline_t *in = priv;
    model3d *m = e->txmodel->model;
    int joint = incline_joint(cfg, m);

    if (joint < 0 || (unsigned int)joint >= m->nr_joints)   return CERR_INVALID_ARGUMENTS;

    if (cfg->rate > 0.0f)   vec3_approach(in->up, in->normal, cfg->rate, (float)dt);
    else                    vec3_dup(in->up, in->normal);
    vec3_norm(in->up, in->up);

    vec3 up, to, axis;
    incline_dir_in_parent(e, joint, (vec3){ 0.0f, 1.0f, 0.0f }, up);
    incline_dir_in_parent(e, joint, in->up, to);
    vec3_mul_cross(axis, up, to);

    /* out[joint] is read while quat_mul() writes it */
    quat tilt, posed;
    memcpy(posed, out[joint].rotation, sizeof(quat));
    quat_from_axis_angle(tilt, axis, atan2f(vec3_len(axis), vec3_mul_inner(up, to)));
    quat_mul(out[joint].rotation, tilt, posed);

    return CERR_OK;
}

const anim_pose_ops incline_pose_ops = {
    .start  = incline_start,
    .eval   = incline_eval,
};

cerr incline_mask(const incline_config *cfg, animator_t *a, model3d *m, unsigned int layer)
{
    int joint = incline_joint(cfg, m);

    if (joint < 0 || (unsigned int)joint >= m->nr_joints)   return CERR_INVALID_ARGUMENTS;

    return animator_mask_chain(a, layer, joint, 1);
}
