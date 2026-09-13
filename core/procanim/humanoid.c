// SPDX-License-Identifier: Apache-2.0
/*
 * The biped orchestrator: a procedurally meshed and rigged humanoid.
 * The body plan (humanoid_config) stacks a spine, a head and hanging
 * arms over the pelvis and hangs a leg column under each hip; a
 * pluggable mesh generator fleshes the skeleton out, so the same
 * armature walks as the cube construct (humanoid_mesh_cubes()) or
 * anything the caller emits over the joints. Steering drives the body,
 * measured speed picks the animation (idle breathing, walk, run and
 * everything in between by blending the two tunings), gait turns time
 * into foot targets, IK turns targets into leg poses, arms swing in
 * counter-phase and the spine leans into the run. The entity update
 * callback is installed by humanoid_gait_start() and chains to
 * whatever was there before.
 */
#include <math.h>
#include "common.h"
#include "clap.h"
#include "interp.h"
#include "scene.h"
#include "procanim/humanoid.h"
#include "procanim/sway.h"

/* the walking reference tuning */
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

/* the running reference tuning */
#define HUM_RUN_CYCLE   0.7f
#define HUM_RUN_SWING   0.56f
#define HUM_RUN_STRIDE  0.85f
#define HUM_RUN_STEP_H  0.24f
#define HUM_RUN_BASE_Y  -0.14f
#define HUM_RUN_BOB     0.05f
#define HUM_RUN_ARM     0.8f
#define HUM_RUN_LEAN    0.14f

/* idling */
#define HUM_IDLE_ENTER  0.08f
#define HUM_IDLE_EXIT   0.18f
#define HUM_IDLE_SETTLE 4.0f
#define HUM_IDLE_BOB    0.015f
#define HUM_IDLE_BOB_F  2.2f
#define HUM_IDLE_SWAY   0.02f
/* breathing sway timing */
#define HUM_IDLE_SWAY_F 0.5f
#define HUM_IDLE_SWAY_LAG 0.3f

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
        .speed          = HUM_STRIDE / HUM_CYCLE,
        .knee_dir       = { 0.0f, 0.0f, 1.0f },
        .leg_joints     = HUM_LEG_JOINTS,
        .shoulders      = { "shoulder_l", "shoulder_r" },
    };
}

void gait_config_run_init(gait_config *cfg)
{
    gait_config_init(cfg);

    cfg->cycle          = HUM_RUN_CYCLE;
    cfg->swing          = HUM_RUN_SWING;
    cfg->stride         = HUM_RUN_STRIDE;
    cfg->step_height    = HUM_RUN_STEP_H;
    cfg->base_y         = HUM_RUN_BASE_Y;
    cfg->bob            = HUM_RUN_BOB;
    cfg->arm_swing      = HUM_RUN_ARM;
    cfg->lean           = HUM_RUN_LEAN;
    cfg->speed          = HUM_RUN_STRIDE / HUM_RUN_CYCLE;
}

void idle_config_init(idle_config *cfg)
{
    *cfg = (idle_config){
        .enter_speed    = HUM_IDLE_ENTER,
        .exit_speed     = HUM_IDLE_EXIT,
        .settle_rate    = HUM_IDLE_SETTLE,
        .bob            = HUM_IDLE_BOB,
        .bob_frequency  = HUM_IDLE_BOB_F,
        .sway_amplitude = HUM_IDLE_SWAY,
    };
}

/* the reference body plan, model space */
#define HUM_PELVIS_Y    1.32f
#define HUM_SPINE_Y     1.82f
#define HUM_CHEST_Y     2.34f
#define HUM_HEAD_Y      2.88f
#define HUM_SHOULDER_X  0.52f
#define HUM_SHOULDER_Y  2.48f
#define HUM_ARM_X       0.60f
#define HUM_ARM_UPPER_Y 2.02f
#define HUM_ARM_LOWER_Y 1.56f
#define HUM_HAND_Y      1.14f
#define HUM_HIP_Y       1.15f
#define HUM_SHIN_Y      0.62f

void humanoid_config_init(humanoid_config *cfg)
{
    *cfg = (humanoid_config){
        .pelvis_y       = HUM_PELVIS_Y,
        .spine_y        = HUM_SPINE_Y,
        .chest_y        = HUM_CHEST_Y,
        .head_y         = HUM_HEAD_Y,
        .shoulder_x     = HUM_SHOULDER_X,
        .shoulder_y     = HUM_SHOULDER_Y,
        .arm_x          = HUM_ARM_X,
        .arm_upper_y    = HUM_ARM_UPPER_Y,
        .arm_lower_y    = HUM_ARM_LOWER_Y,
        .hand_y         = HUM_HAND_Y,
        /* the legs share the walk tuning's lateral offset and planted height */
        .hip_x          = HUM_HIP_X,
        .hip_y          = HUM_HIP_Y,
        .shin_y         = HUM_SHIN_Y,
        .foot_y         = HUM_FOOT_Y,
        .size = {
            [HUMANOID_PELVIS]       = 0.55f,
            [HUMANOID_SPINE]        = 0.60f,
            [HUMANOID_CHEST]        = 0.68f,
            [HUMANOID_HEAD]         = 0.40f,
            [HUMANOID_SHOULDER_L]   = 0.30f,    [HUMANOID_SHOULDER_R]   = 0.30f,
            [HUMANOID_ARM_UPPER_L]  = 0.34f,    [HUMANOID_ARM_UPPER_R]  = 0.34f,
            [HUMANOID_ARM_LOWER_L]  = 0.30f,    [HUMANOID_ARM_LOWER_R]  = 0.30f,
            [HUMANOID_HAND_L]       = 0.26f,    [HUMANOID_HAND_R]       = 0.26f,
            [HUMANOID_HIP_L]        = 0.34f,    [HUMANOID_HIP_R]        = 0.34f,
            [HUMANOID_SHIN_L]       = 0.30f,    [HUMANOID_SHIN_R]       = 0.30f,
            [HUMANOID_FOOT_L]       = 0.34f,    [HUMANOID_FOOT_R]       = 0.34f,
        },
    };
}

/* fill the joint table from the body plan */
static void humanoid_skeleton_build(humanoid *h)
{
    const humanoid_config *cfg = &h->cfg;
    const skel_joint sk[HUMANOID_NR_JOINTS] = {
        [HUMANOID_PELVIS]      = { "pelvis",      -1,                   {  0.0f,            cfg->pelvis_y,    0.0f } },
        [HUMANOID_SPINE]       = { "spine",       HUMANOID_PELVIS,      {  0.0f,            cfg->spine_y,     0.0f } },
        [HUMANOID_CHEST]       = { "chest",       HUMANOID_SPINE,       {  0.0f,            cfg->chest_y,     0.0f } },
        [HUMANOID_HEAD]        = { "head",        HUMANOID_CHEST,       {  0.0f,            cfg->head_y,      0.0f } },
        [HUMANOID_SHOULDER_L]  = { "shoulder_l",  HUMANOID_CHEST,       { -cfg->shoulder_x, cfg->shoulder_y,  0.0f } },
        [HUMANOID_ARM_UPPER_L] = { "arm_upper_l", HUMANOID_SHOULDER_L,  { -cfg->arm_x,      cfg->arm_upper_y, 0.0f } },
        [HUMANOID_ARM_LOWER_L] = { "arm_lower_l", HUMANOID_ARM_UPPER_L, { -cfg->arm_x,      cfg->arm_lower_y, 0.0f } },
        [HUMANOID_HAND_L]      = { "hand_left",   HUMANOID_ARM_LOWER_L, { -cfg->arm_x,      cfg->hand_y,      0.0f } },
        [HUMANOID_SHOULDER_R]  = { "shoulder_r",  HUMANOID_CHEST,       {  cfg->shoulder_x, cfg->shoulder_y,  0.0f } },
        [HUMANOID_ARM_UPPER_R] = { "arm_upper_r", HUMANOID_SHOULDER_R,  {  cfg->arm_x,      cfg->arm_upper_y, 0.0f } },
        [HUMANOID_ARM_LOWER_R] = { "arm_lower_r", HUMANOID_ARM_UPPER_R, {  cfg->arm_x,      cfg->arm_lower_y, 0.0f } },
        [HUMANOID_HAND_R]      = { "hand_right",  HUMANOID_ARM_LOWER_R, {  cfg->arm_x,      cfg->hand_y,      0.0f } },
        [HUMANOID_HIP_L]       = { "hip_l",       HUMANOID_PELVIS,      { -cfg->hip_x,      cfg->hip_y,       0.0f } },
        [HUMANOID_SHIN_L]      = { "shin_l",      HUMANOID_HIP_L,       { -cfg->hip_x,      cfg->shin_y,      0.0f } },
        [HUMANOID_FOOT_L]      = { "foot_left",   HUMANOID_SHIN_L,      { -cfg->hip_x,      cfg->foot_y,      0.0f } },
        [HUMANOID_HIP_R]       = { "hip_r",       HUMANOID_PELVIS,      {  cfg->hip_x,      cfg->hip_y,       0.0f } },
        [HUMANOID_SHIN_R]      = { "shin_r",      HUMANOID_HIP_R,       {  cfg->hip_x,      cfg->shin_y,      0.0f } },
        [HUMANOID_FOOT_R]      = { "foot_right",  HUMANOID_SHIN_R,      {  cfg->hip_x,      cfg->foot_y,      0.0f } },
    };

    for (int j = 0; j < HUMANOID_NR_JOINTS; j++) {
        h->skel[j] = sk[j];
        h->skel[j].size = cfg->size[j];
    }
}

cerr humanoid_mesh_cubes(mesh_t *m, humanoid *h)
{
    return skeleton_emit(.mesh = m, .joints = h->skel, .nr_joints = HUMANOID_NR_JOINTS);
}

cresp(model3d) humanoid_model(humanoid *h, pipeline *pl)
{
    if (!h || !pl)  return cresp_error(model3d, CERR_INVALID_ARGUMENTS);

    if (h->cfg.pelvis_y == 0.0f)    humanoid_config_init(&h->cfg);

    humanoid_skeleton_build(h);

    LOCAL_SET(mesh_t, m) = CRES_RET_T(
        ref_new_checked(mesh, .name = h->name ? : "humanoid"),
        model3d
    );

    humanoid_mesh_fn mesh_fn = h->mesh ? : humanoid_mesh_cubes;
    h->rigid_nr = 0;
    CERR_RET_T(mesh_fn(m, h), model3d);

    /*
     * A generator that skinned the mesh itself (skeleton_emit() binds a
     * rigid cube to each joint) is left alone; one that only emitted
     * geometry gets the nearest-bone binding, with whatever it declared
     * rigid rebound to the pelvis alone: the automatic skinning weighs
     * such verts to the limb bones swinging past them, and the limbs
     * then drag a hull out of shape.
     */
    if (!mesh_joints(m)) {
        CERR_RET_T(
            skeleton_skin(.mesh = m, .joints = h->skel, .nr_joints = HUMANOID_NR_JOINTS),
            model3d
        );

        unsigned char *vx_joints = mesh_joints(m);
        float *vx_weights = mesh_weights(m);
        for (size_t v = 0; v < h->rigid_nr && v < mesh_nr_joints(m); v++) {
            memset(vx_joints + v * 4, 0, 4);
            vx_weights[v * 4 + 0] = 1.0f;
            vx_weights[v * 4 + 1] = vx_weights[v * 4 + 2] = vx_weights[v * 4 + 3] = 0.0f;
        }
    }
    mesh_aabb_calc(m);

    return skeleton_model_new(pl, m, h->skel, HUMANOID_NR_JOINTS, h->name ? : "humanoid");
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

    vec3 pos;
    transform_pos(&e->xform, pos);

    /* speed measured from the body's own horizontal motion */
    if (dt > 0.0f) {
        vec3 moved;

        vec3_sub(moved, pos, h->prev_pos);
        moved[1] = 0.0f;
        h->speed = linf_interp(h->speed, vec3_len(moved) / dt, approachf(HUM_SPEED_RATE, dt));
    }
    vec3_dup(h->prev_pos, pos);

    /* idle hysteresis, then ease every layer toward its target level */
    if (h->idling ? h->speed > h->idle.exit_speed : h->speed < h->idle.enter_speed) h->idling = !h->idling;

    float settle = approachf(h->idle.settle_rate, dt);
    if (!h->idling)
        h->blend = linf_interp(h->blend,
                               smoothstepf(h->walk.speed, h->run.speed, h->speed), settle);
    h->activity = linf_interp(h->activity, h->idling ? 0.0f : 1.0f, settle);

    /* blended gait parameters; the phase integrates through changes */
    h->gait.cycle       = linf_interp(h->walk.cycle, h->run.cycle, h->blend);
    h->gait.swing       = linf_interp(h->walk.swing, h->run.swing, h->blend);
    h->gait.stride      = linf_interp(h->walk.stride, h->run.stride, h->blend);
    h->gait.step_height = linf_interp(h->walk.step_height, h->run.step_height, h->blend);

    /* the humanoid owns the vertical: ground, crouch base, gait bob, breathing */
    float bob = linf_interp(h->walk.bob, h->run.bob, h->blend) * h->activity *
                sinf(4.0f * (float)M_PI * h->gait.phase)
              + h->idle.bob * (1.0f - h->activity) *
                sinf((float)t * h->idle.bob_frequency);
    float ground = h->ground ? h->ground(h->ground_priv, pos[0], pos[2]) : 0.0f;

    pos[1] = ground + linf_interp(h->walk.base_y, h->run.base_y, h->blend) + bob;
    entity3d_position(e, pos);

    vec3 forward = { 0.0f, 0.0f, 1.0f }, right = { 1.0f, 0.0f, 0.0f };
    transform_rotate_vec3(&e->xform, forward);
    transform_rotate_vec3(&e->xform, right);

    entity3d_pose_reset(e);

    /* arms swing in counter-phase, easing out at rest */
    float arm_swing = linf_interp(h->walk.arm_swing, h->run.arm_swing, h->blend) * h->activity;
    for (int a = 0; a < 2; a++) {
        if (h->shoulders[a] < 0)    continue;

        float swing = arm_swing * sinf(2.0f * (float)M_PI * h->gait.phase) * (a ? 1.0f : -1.0f);

        quat_from_axis_angle(e->joints[h->shoulders[a]].rotation,
                             (vec3){ 1.0f, 0.0f, 0.0f }, swing);
    }

    /* spine: breathe at rest, lean into the run on top of it */
    if (h->walk.spine_nr) {
        float breathe = h->idle.sway_amplitude * (1.0f - h->activity);
        if (breathe > 1e-4f) {
            cerr err = chain_sway(e, t, &(sway_config){
                .wind       = { forward[0], 0.0f, forward[2] },
                .first      = h->walk.spine_first,
                .nr         = h->walk.spine_nr,
                .amplitude  = breathe,
                .frequency  = HUM_IDLE_SWAY_F,
                .phase_lag  = HUM_IDLE_SWAY_LAG,
                .no_gust    = true,
                .no_flutter = true,
                .no_twist   = true,
            });
            if (IS_CERR(err))   warn_once("breathing sway failed\n");
        }

        float lean = linf_interp(h->walk.lean, h->run.lean, h->blend) * h->activity / h->walk.spine_nr;
        quat q;

        quat_from_axis_angle(q, (vec3){ 1.0f, 0.0f, 0.0f }, lean);
        for (unsigned int j = 0; j < h->walk.spine_nr; j++) {
            float *rot = e->joints[h->walk.spine_first + j].rotation;
            quat r;

            quat_mul(r, rot, q);
            vec4_dup(rot, r);
        }
    }

    /*
     * Step while moving; when idling, let airborne swings land, then
     * hold the feet where they are
     */
    if (!h->idling || h->feet[0].swinging || h->feet[1].swinging) {
        cerr err = gait_update(&h->gait, pos, forward, right, h->speed, dt);
        if (IS_CERR(err))   warn_once("gait_update failed\n");
    }

    for (int f = 0; f < 2; f++) {
        vec3 target;

        entity3d_world_to_model(e, h->feet[f].target, target);

        cerr err = ik_chain_solve(&h->legs[f],
                                  .target = { target[0], target[1], target[2] },
                                  .pole   = { h->walk.knee_dir[0], h->walk.knee_dir[1],
                                              h->walk.knee_dir[2] });
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

    if (h->walk.cycle == 0.0f)          gait_config_init(&h->walk);
    if (h->run.cycle == 0.0f)           gait_config_run_init(&h->run);
    if (h->idle.enter_speed == 0.0f)    idle_config_init(&h->idle);

    model3d *model = e->txmodel->model;
    if (h->walk.spine_first + h->walk.spine_nr > model->nr_joints)  return CERR_INVALID_ARGUMENTS;

    cres(joint_type) foot_l = model3d_get_joint(model, JOINT_FOOT_LEFT);
    cres(joint_type) foot_r = model3d_get_joint(model, JOINT_FOOT_RIGHT);

    if (IS_CERR(foot_l) || IS_CERR(foot_r)) return CERR_INVALID_ARGUMENTS;

    CERR_RET_CERR(ik_chain_init(&h->legs[0], e, foot_l.val, h->walk.leg_joints));
    CERR_RET(
        ik_chain_init(&h->legs[1], e, foot_r.val, h->walk.leg_joints),
        { ik_chain_done(&h->legs[0]); return __cerr; }
    );

    for (int a = 0; a < 2; a++) {
        h->shoulders[a] = -1;
        if (!h->walk.shoulders[a])  continue;

        cres(int) sh = model3d_joint_by_name(model, h->walk.shoulders[a]);
        if (!IS_CERR(sh))   h->shoulders[a] = sh.val;
    }

    h->feet[0] = (gait_leg){ .offset = { -h->walk.hip_x, 0.0f, 0.0f }, .phase = 0.0f };
    h->feet[1] = (gait_leg){ .offset = {  h->walk.hip_x, 0.0f, 0.0f }, .phase = 0.5f };
    h->gait = (gait){
        .legs           = h->feet,
        .nr_legs        = 2,
        .cycle          = h->walk.cycle,
        .swing          = h->walk.swing,
        .stride         = h->walk.stride,
        .step_height    = h->walk.step_height,
        .foot_y         = h->walk.foot_y,
        .ground         = h->ground,
        .ground_priv    = h->ground_priv,
    };

    vec3 pos, forward = { 0.0f, 0.0f, 1.0f }, right = { 1.0f, 0.0f, 0.0f };

    transform_pos(&e->xform, pos);
    transform_rotate_vec3(&e->xform, forward);
    transform_rotate_vec3(&e->xform, right);
    gait_reset(&h->gait, pos, forward, right);

    vec3_dup(h->prev_pos, pos);
    h->speed = 0.0f;
    h->blend = 0.0f;
    h->activity = 1.0f;
    h->idling = false;
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
