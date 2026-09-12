// SPDX-License-Identifier: Apache-2.0
/*
 * The biped orchestrator: steering drives the body, gait turns time
 * into foot targets, IK turns targets into leg poses, arms swing in
 * counter-phase. Everything tunable lives in gait_config; the entity
 * update callback is installed by humanoid_gait_start() and chains to
 * whatever was there before.
 */
#include <math.h>
#include "common.h"
#include "clap.h"
#include "interp.h"
#include "scene.h"
#include "procanim/humanoid.h"

/* the reference tuning */
#define HUM_CYCLE       1.1f
#define HUM_SWING       0.38f
#define HUM_STRIDE      0.55f
#define HUM_STEP_H      0.16f
#define HUM_FOOT_Y      0.17f
#define HUM_HIP_X       0.28f
#define HUM_BASE_Y      -0.08f
#define HUM_BOB         0.03f
#define HUM_ARM_SWING   0.45f
#define HUM_LEG_JOINTS  3
/* horizontal speed low-pass rate, 1/s */
#define HUM_SPEED_RATE  8.0f

void gait_config_init(gait_config *cfg)
{
    *cfg = (gait_config){
        .cycle          = HUM_CYCLE,
        .swing          = HUM_SWING,
        .stride         = HUM_STRIDE,
        .step_height    = HUM_STEP_H,
        .foot_y         = HUM_FOOT_Y,
        .hip_x          = HUM_HIP_X,
        .base_y         = HUM_BASE_Y,
        .bob            = HUM_BOB,
        .arm_swing      = HUM_ARM_SWING,
        .knee_dir       = { 0.0f, 0.0f, 1.0f },
        .leg_joints     = HUM_LEG_JOINTS,
        .shoulders      = { "shoulder_l", "shoulder_r" },
    };
}

static int humanoid_update(entity3d *e, void *data)
{
    humanoid *h = e->priv;
    struct scene *s = data;
    double t = clap_get_current_time(s->clap_ctx);

    if (!h->t0) {
        h->t0 = t;
        h->t_last = 0.0;
        transform_pos(&e->xform, h->prev_pos);
    }
    t -= h->t0;

    float dt = (float)(t - h->t_last);
    h->t_last = t;

    if (h->steer)
        h->steer(h, t, dt);

    /* the humanoid owns the vertical: crouch base plus bob */
    float phase = (float)(t / h->cfg.cycle - floor(t / h->cfg.cycle));
    vec3 pos;

    transform_pos(&e->xform, pos);
    pos[1] = h->cfg.base_y + h->cfg.bob * sinf(4.0f * (float)M_PI * phase);
    entity3d_position(e, pos);

    vec3 forward = { 0.0f, 0.0f, 1.0f }, right = { 1.0f, 0.0f, 0.0f };
    transform_rotate_vec3(&e->xform, forward);
    transform_rotate_vec3(&e->xform, right);

    /* speed measured from the body's own horizontal motion */
    if (dt > 0.0f) {
        vec3 moved;

        vec3_sub(moved, pos, h->prev_pos);
        moved[1] = 0.0f;
        h->speed = linf_interp(h->speed, vec3_len(moved) / dt, approachf(HUM_SPEED_RATE, dt));
    }
    vec3_dup(h->prev_pos, pos);

    entity3d_pose_reset(e);

    for (int a = 0; a < 2; a++) {
        if (h->shoulders[a] < 0)    continue;

        float swing = h->cfg.arm_swing * sinf(2.0f * (float)M_PI * phase) * (a ? 1.0f : -1.0f);

        quat_from_axis_angle(e->joints[h->shoulders[a]].rotation,
                             (vec3){ 1.0f, 0.0f, 0.0f }, swing);
    }

    cerr err = gait_update(&h->gait, pos, forward, right, h->speed, dt);
    if (IS_CERR(err))
        warn_once("gait_update failed\n");

    for (int f = 0; f < 2; f++) {
        vec3 target;

        entity3d_world_to_model(e, h->feet[f].target, target);

        err = ik_chain_solve(&h->legs[f],
                             .target = { target[0], target[1], target[2] },
                             .pole   = { h->cfg.knee_dir[0], h->cfg.knee_dir[1], h->cfg.knee_dir[2] });
        if (IS_CERR(err))   warn_once("leg IK failed\n");
    }

    return h->orig_update ? h->orig_update(e, data) : 0;
}

cerr humanoid_gait_start(humanoid *h)
{
    entity3d *e = h->e;

    if (!e || !h->scene || !e->joints)
        return CERR_INVALID_ARGUMENTS;
    if (e->priv)
        return CERR_INVALID_OPERATION;

    if (h->cfg.cycle == 0.0f)
        gait_config_init(&h->cfg);

    model3d *model = e->txmodel->model;
    cres(joint_type) foot_l = model3d_get_joint(model, JOINT_FOOT_LEFT);
    cres(joint_type) foot_r = model3d_get_joint(model, JOINT_FOOT_RIGHT);

    if (IS_CERR(foot_l) || IS_CERR(foot_r)) return CERR_INVALID_ARGUMENTS;

    CERR_RET_CERR(ik_chain_init(&h->legs[0], e, foot_l.val, h->cfg.leg_joints));
    CERR_RET(
        ik_chain_init(&h->legs[1], e, foot_r.val, h->cfg.leg_joints),
        { ik_chain_done(&h->legs[0]); return __cerr; }
    );

    for (int a = 0; a < 2; a++) {
        h->shoulders[a] = -1;
        if (!h->cfg.shoulders[a])
            continue;

        cres(int) sh = model3d_joint_by_name(model, h->cfg.shoulders[a]);
        if (!IS_CERR(sh))   h->shoulders[a] = sh.val;
    }

    h->feet[0] = (gait_leg){ .offset = { -h->cfg.hip_x, 0.0f, 0.0f }, .phase = 0.0f };
    h->feet[1] = (gait_leg){ .offset = {  h->cfg.hip_x, 0.0f, 0.0f }, .phase = 0.5f };
    h->gait = (gait){
        .legs           = h->feet,
        .nr_legs        = 2,
        .cycle          = h->cfg.cycle,
        .swing          = h->cfg.swing,
        .stride         = h->cfg.stride,
        .step_height    = h->cfg.step_height,
        .foot_y         = h->cfg.foot_y,
    };

    vec3 pos, forward = { 0.0f, 0.0f, 1.0f }, right = { 1.0f, 0.0f, 0.0f };

    transform_pos(&e->xform, pos);
    transform_rotate_vec3(&e->xform, forward);
    transform_rotate_vec3(&e->xform, right);
    gait_reset(&h->gait, pos, forward, right);

    vec3_dup(h->prev_pos, pos);
    h->speed = 0.0f;
    h->t0 = 0.0;
    h->t_last = 0.0;

    entity3d_pose_reset(e);
    entity3d_pose_update(e);

    h->orig_update = e->update;
    e->update = humanoid_update;
    e->priv = h;

    return CERR_OK;
}

void humanoid_gait_stop(humanoid *h)
{
    entity3d *e = h->e;

    if (!e || e->update != humanoid_update)
        return;

    e->update = h->orig_update;
    e->priv = nullptr;
    h->orig_update = nullptr;

    ik_chain_done(&h->legs[0]);
    ik_chain_done(&h->legs[1]);
}
