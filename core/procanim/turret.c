// SPDX-License-Identifier: Apache-2.0
/*
 * A yaw joint and a pitch joint, its child, aimed at a point: a cannon on
 * a hull, a gun on a mount. The point goes into the yaw joint's parent
 * frame through the pose so far; the yaw is measured there, the pitch in
 * the yawed frame from the pitch joint's pivot, both clamped and slewed.
 */
#include <math.h>
#include "interp.h"
#include "model.h"
#include "util.h"
#include "procanim/turret.h"

static const vec3 turret_forward = { 0.0f, 0.0f, 1.0f };
static const vec3 turret_up = { 0.0f, 1.0f, 0.0f };

static double wrap_pi(double a)
{
    while (a >  M_PI)   a -= 2.0 * M_PI;
    while (a < -M_PI)   a += 2.0 * M_PI;

    return a;
}

static void turret_start(const void *_cfg, void *priv, entity3d *e)
{
    const turret_config *cfg = _cfg;
    turret_t *tr = priv;

    target_start(&tr->target, &cfg->law);
}

static cerr turret_eval(const void *_cfg, void *priv, entity3d *e, double t, double dt,
                        struct joint *out)
{
    const turret_config *cfg = _cfg;
    turret_t *tr = priv;
    model3d *m = e->txmodel->model;
    int yj = cfg->yaw_joint, pj = cfg->pitch_joint;

    if (yj < 0 || (unsigned int)yj >= m->nr_joints || pj < 0 || (unsigned int)pj >= m->nr_joints ||
        m->joints[pj].parent != yj)
        return CERR_INVALID_ARGUMENTS;

    vec3 point;
    double want_yaw = 0.0, want_pitch = 0.0;
    bool limited_yaw = cfg->yaw_min != 0.0 || cfg->yaw_max != 0.0;

    target_update(&tr->target, &cfg->law, e, t, dt, point);

    if (!tr->target.none) {
        vec3 local, dir;

        entity3d_point_in_parent(e, yj, point, local);
        vec3_sub(dir, local, out[yj].translation);

        double unused;
        aim_angles(out[yj].rotation, turret_forward, turret_up, dir, &want_yaw, &unused);
        if (limited_yaw)    want_yaw = clampd(want_yaw, cfg->yaw_min, cfg->yaw_max);

        /* the pitch from the pivot, in the yawed frame the pitch joint hangs in */
        quat yawed;
        vec3 pivot, pdir;

        aim_rotate(yawed, out[yj].rotation, turret_forward, turret_up, want_yaw, 0.0);
        quat_mul_vec3(pivot, yawed, out[pj].translation);
        vec3_add(pivot, pivot, out[yj].translation);
        vec3_sub(pdir, local, pivot);

        quat pitch_rest;
        quat_mul(pitch_rest, yawed, out[pj].rotation);
        aim_angles(pitch_rest, turret_forward, turret_up, pdir, &unused, &want_pitch);
        if (cfg->pitch_min != 0.0 || cfg->pitch_max != 0.0)
            want_pitch = clampd(want_pitch, cfg->pitch_min, cfg->pitch_max);
    }

    /* an unlimited yaw takes the short way around */
    double yerr = limited_yaw ? want_yaw - tr->yaw : wrap_pi(want_yaw - tr->yaw);

    tr->yaw = step_toward(tr->yaw, tr->yaw + yerr, cfg->slew, dt);
    tr->pitch = step_toward(tr->pitch, want_pitch, cfg->slew, dt);
    tr->yaw_err = limited_yaw ? want_yaw - tr->yaw : wrap_pi(want_yaw - tr->yaw);
    tr->pitch_err = want_pitch - tr->pitch;
    if (!limited_yaw)   tr->yaw = wrap_pi(tr->yaw);

    aim_rotate(out[yj].rotation, out[yj].rotation, turret_forward, turret_up, tr->yaw, 0.0);
    aim_rotate(out[pj].rotation, out[pj].rotation, turret_forward, turret_up, 0.0, tr->pitch);

    return CERR_OK;
}

const anim_pose_ops turret_pose_ops = {
    .start  = turret_start,
    .eval   = turret_eval,
};
