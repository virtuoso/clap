// SPDX-License-Identifier: Apache-2.0
/*
 * One joint's forward axis at a point: a character's head at what it
 * looks at. The joint's parent is posed already (the base layer wrote it),
 * so the point is taken into the parent's frame through the pose so far,
 * the yaw and pitch from the posed forward are clamped and slewed, and the
 * result is a delta rotation on top of the posed one.
 */
#include <math.h>
#include "interp.h"
#include "model.h"
#include "util.h"
#include "procanim/lookat.h"

static int lookat_joint(const lookat_config *cfg, model3d *m)
{
    if (cfg->joint >= 0)    return cfg->joint;

    cres(joint_type) jt = model3d_get_joint(m, cfg->joint_type);
    return IS_CERR(jt) ? -1 : (int)jt.val;
}

/*
 * The joint's axes in its own frame. A zero axis in the configuration is
 * the model's (+Z forward, +Y up) taken into the joint's bind frame: the
 * bind matrix's rotation transposed, i.e. its rows, normalized.
 */
static void lookat_axes(const lookat_config *cfg, model3d *m, int joint, vec3 forward, vec3 up)
{
    mat4x4 *bind = &m->joints[joint].bind;

    if (vec3_mul_inner(cfg->forward, cfg->forward) > 0.0f)
        vec3_dup(forward, (float *)cfg->forward);
    else
        vec3_norm(forward, (vec3){ (*bind)[0][2], (*bind)[1][2], (*bind)[2][2] });

    if (vec3_mul_inner(cfg->up, cfg->up) > 0.0f)
        vec3_dup(up, (float *)cfg->up);
    else
        vec3_norm(up, (vec3){ (*bind)[0][1], (*bind)[1][1], (*bind)[2][1] });
}

/* a step of at most slew * dt toward want; unlimited slew arrives */
static void lookat_start(const void *_cfg, void *priv, entity3d *e)
{
    const lookat_config *cfg = _cfg;
    lookat_t *l = priv;

    target_start(&l->target, &cfg->law);
}

static cerr lookat_eval(const void *_cfg, void *priv, entity3d *e, double t, double dt,
                        struct joint *out)
{
    const lookat_config *cfg = _cfg;
    lookat_t *l = priv;
    model3d *m = e->txmodel->model;
    int joint = lookat_joint(cfg, m);

    if (joint < 0 || (unsigned int)joint >= m->nr_joints)   return CERR_INVALID_ARGUMENTS;

    vec3 point, forward, up;
    double want_yaw = 0.0, want_pitch = 0.0;

    target_update(&l->target, &cfg->law, e, t, dt, point);
    lookat_axes(cfg, m, joint, forward, up);

    if (!l->target.none) {
        vec3 local, dir;

        entity3d_point_in_parent(e, joint, point, local);
        vec3_sub(dir, local, out[joint].translation);

        aim_angles(out[joint].rotation, forward, up, dir, &want_yaw, &want_pitch);
        if (cfg->yaw_max > 0.0)     want_yaw = clampd(want_yaw, -cfg->yaw_max, cfg->yaw_max);
        if (cfg->pitch_max > 0.0)   want_pitch = clampd(want_pitch, -cfg->pitch_max, cfg->pitch_max);
    }

    l->yaw = step_toward(l->yaw, want_yaw, cfg->slew, dt);
    l->pitch = step_toward(l->pitch, want_pitch, cfg->slew, dt);

    aim_rotate(out[joint].rotation, out[joint].rotation, forward, up, l->yaw, l->pitch);

    return CERR_OK;
}

const anim_pose_ops lookat_pose_ops = {
    .start  = lookat_start,
    .eval   = lookat_eval,
};
