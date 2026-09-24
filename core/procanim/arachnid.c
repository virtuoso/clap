// SPDX-License-Identifier: Apache-2.0
/*
 * The eight-legged orchestrator: a procedurally meshed and rigged
 * arachnid. The body plan (arachnid_config) places a cephalothorax, an
 * abdomen and eight three-joint legs fanned around the body; a
 * pluggable mesh generator fleshes the skeleton out, so the same
 * armature walks as a garden spider (arachnid_mesh_organic()), a spider
 * tank (arachnid_mesh_tank()) or anything the caller emits over the
 * joints. Locomotion mirrors the humanoid: steering drives the body,
 * the generic gait turns its motion into world-space foot targets in an
 * alternating-tetrapod pattern, and one IK chain per leg aims at them
 * with a knee-up pole. The whole of it is a pose class
 * (arachnid_pose_ops, core/animator.h): the animator calls it every
 * frame the rig plays on an entity it is bound to.
 */
#include <math.h>
#include "common.h"
#include "clap.h"
#include "interp.h"
#include "primitives.h"
#include "scene.h"
#include "animator.h"
#include "procanim/arachnid.h"

/*
 * The reference body plan, a garden spider, model space. Real
 * spiders carry all eight legs on the prosoma (cephalothorax), a compact
 * segment ahead of the big legless abdomen. So the legs cluster at
 * ARA_FAN_Z, the roots spread fore/aft onto both lobes, and the abdomen
 * sits back and low, out of the legs' way.
 */
#define ARA_CEPH_Y      1.22f
#define ARA_CEPH_Z      0.34f
#define ARA_CEPH_R      0.54f
#define ARA_ABD_Y       0.95f
#define ARA_ABD_Z      -0.55f
#define ARA_ABD_R       0.74f
#define ARA_HIP_Y       0.97f
#define ARA_HIP_R       0.42f
#define ARA_FAN_Z       0.30f
#define ARA_ROOT_SPREAD 0.55f
#define ARA_KNEE_Y      1.42f
#define ARA_KNEE_R      1.15f
#define ARA_FOOT_Y      0.06f
#define ARA_FOOT_R      1.85f

/* the reference gait tuning */
#define ARA_CYCLE       0.62f
#define ARA_SWING       0.45f
#define ARA_STRIDE      0.70f
#define ARA_STEP_H      0.18f

/* horizontal speed low-pass rate, 1/s */
#define ARA_SPEED_RATE  8.0f

void arachnid_config_init(arachnid_config *cfg)
{
    *cfg = (arachnid_config){
        .ceph_y         = ARA_CEPH_Y,
        .ceph_z         = ARA_CEPH_Z,
        .ceph_r         = ARA_CEPH_R,
        .abd_y          = ARA_ABD_Y,
        .abd_z          = ARA_ABD_Z,
        .abd_r          = ARA_ABD_R,
        .hip_y          = ARA_HIP_Y,
        .hip_r          = ARA_HIP_R,
        .fan_z          = ARA_FAN_Z,
        .root_spread    = ARA_ROOT_SPREAD,
        .knee_y         = ARA_KNEE_Y,
        .knee_r         = ARA_KNEE_R,
        .foot_y         = ARA_FOOT_Y,
        .foot_r         = ARA_FOOT_R,
        .leg_r          = { 0.10f, 0.075f, 0.03f },
        /*
         * The front pair sits well forward (converging), the rear pair
         * sweeps back but keeps enough sideways splay to clear the
         * abdomen.
         */
        .fan            = { 56.0f, 20.0f, -14.0f, -38.0f },
        .cycle          = ARA_CYCLE,
        .swing          = ARA_SWING,
        .stride         = ARA_STRIDE,
        .step_height    = ARA_STEP_H,
    };
}

/* leg direction in the body frame: front-to-back fan, mirrored per side */
static void arachnid_leg_dir(const arachnid_config *cfg, int leg, vec3 dir)
{
    int side = leg < ARACHNID_NR_LEGS / 2 ? 1 : -1;
    float phi = to_radians(cfg->fan[leg % (ARACHNID_NR_LEGS / 2)]);

    dir[0] = side * cosf(phi);
    dir[1] = 0.0f;
    dir[2] = sinf(phi);
}

/* fill the joint table, the gait legs and the IK poles from the fan */
static void arachnid_skeleton_build(arachnid *a)
{
    const arachnid_config *cfg = &a->cfg;

    strcpy(a->names[0], "cephalothorax");
    a->skel[0] = (skel_joint){ .name = a->names[0], .parent = -1,
                               .pos = { 0.0f, cfg->ceph_y, cfg->ceph_z } };
    strcpy(a->names[1], "abdomen");
    a->skel[1] = (skel_joint){ .name = a->names[1], .parent = 0,
                               .pos = { 0.0f, cfg->abd_y, cfg->abd_z } };

    for (int leg = 0; leg < ARACHNID_NR_LEGS; leg++) {
        int base = 2 + leg * ARACHNID_LEG_JOINTS;
        vec3 d;

        arachnid_leg_dir(cfg, leg, d);

        snprintf(a->names[base + 0], sizeof(a->names[0]), "hip_%d", leg);
        snprintf(a->names[base + 1], sizeof(a->names[0]), "knee_%d", leg);
        snprintf(a->names[base + 2], sizeof(a->names[0]), "foot_%d", leg);

        /*
         * The root spreads fore/aft more than the knee/foot fan does, so
         * the front pair sits on the cephalothorax and the rear pair
         * back on the abdomen flank; the knee and foot still radiate
         * from the shared fan center.
         */
        a->skel[base + 0] = (skel_joint){ .name = a->names[base + 0], .parent = 0,
            .pos = { d[0] * cfg->hip_r,  cfg->hip_y,  cfg->fan_z + d[2] * cfg->root_spread } };
        a->skel[base + 1] = (skel_joint){ .name = a->names[base + 1], .parent = base + 0,
            .pos = { d[0] * cfg->knee_r, cfg->knee_y, cfg->fan_z + d[2] * cfg->knee_r } };
        a->skel[base + 2] = (skel_joint){ .name = a->names[base + 2], .parent = base + 1,
            .pos = { d[0] * cfg->foot_r, cfg->foot_y, cfg->fan_z + d[2] * cfg->foot_r } };

        /* foot rest spot in the body frame: [0] lateral, [2] fore/aft */
        int rank = leg % (ARACHNID_NR_LEGS / 2);
        bool group = (rank + (leg < ARACHNID_NR_LEGS / 2 ? 0 : 1)) & 1;
        a->feet[leg] = (gait_leg){
            .offset = { d[0] * cfg->foot_r, 0.0f, cfg->fan_z + d[2] * cfg->foot_r },
            .phase  = group ? 0.5f : 0.0f,      /* alternating tetrapod */
        };

        /* the knee bends up and slightly outward */
        vec3_norm(a->pole[leg], (vec3){ d[0] * 0.5f, 1.0f, d[2] * 0.5f });
    }
}

cerr arachnid_mesh_organic(mesh_t *m, arachnid *a)
{
    const arachnid_config *cfg = &a->cfg;

    /* cephalothorax and abdomen */
    CERR_RET_CERR(prim_emit_sphere(a->skel[0].pos, cfg->ceph_r, 12, .mesh = m));
    CERR_RET_CERR(prim_emit_sphere(a->skel[1].pos, cfg->abd_r, 14, .mesh = m));

    for (int leg = 0; leg < ARACHNID_NR_LEGS; leg++) {
        int base = 2 + leg * ARACHNID_LEG_JOINTS;

        CERR_RET_CERR(prim_emit_prism(a->skel[base + 0].pos, a->skel[base + 1].pos,
                                      cfg->leg_r[0], cfg->leg_r[1], 8, 0.0f, .mesh = m));
        CERR_RET_CERR(prim_emit_prism(a->skel[base + 1].pos, a->skel[base + 2].pos,
                                      cfg->leg_r[1], cfg->leg_r[2], 8, 0.0f, .mesh = m));
    }

    return CERR_OK;
}

cerr arachnid_mesh_tank(mesh_t *m, arachnid *a)
{
    const arachnid_config *cfg = &a->cfg;

    /*
     * The hull: a flat eight-sided puck standing over the leg fan. The
     * deck sits at the cephalothorax height, so that joint lands on the
     * deck center - the natural mount for a turret later - and the
     * belly mirrors it about the abdomen center, which makes the
     * abdomen joint the hull's centroid. Wide enough to cover every hip
     * root, with a touch of skirt taper.
     */
    float r = 0.0f;
    for (int leg = 0; leg < ARACHNID_NR_LEGS; leg++) {
        const float *hip = a->skel[2 + leg * ARACHNID_LEG_JOINTS].pos;

        r = fmaxf(r, hypotf(hip[0], hip[2] - cfg->fan_z));
    }
    r += 2.0f * cfg->leg_r[0];

    vec3 belly = { 0.0f, 2.0f * cfg->abd_y - cfg->ceph_y, cfg->fan_z };
    vec3 deck  = { 0.0f, cfg->ceph_y, cfg->fan_z };
    CERR_RET_CERR(prim_emit_prism(belly, deck, r, r * 0.9f, 8,
                                  (float)M_PI / 8.0f, .mesh = m));

    /*
     * The femurs root inside the hull; left whole, their quads sweep
     * through the wall and graze the deck plane as the legs stride
     * (z-fighting dashes where one rides it). Trim each at the point
     * the hip-knee line leaves the hull radius and cover the
     * articulation with a fixed shoulder pod, so the strut pivots at
     * the surface instead of inside the body.
     */
    float trim[ARACHNID_NR_LEGS];
    for (int leg = 0; leg < ARACHNID_NR_LEGS; leg++) {
        int base = 2 + leg * ARACHNID_LEG_JOINTS;
        const float *hip = a->skel[base + 0].pos;
        const float *knee = a->skel[base + 1].pos;
        float hx = hip[0], hz = hip[2] - cfg->fan_z;
        float dx = knee[0] - hip[0], dz = knee[2] - hip[2];
        float qa = dx * dx + dz * dz;
        float qb = 2.0f * (hx * dx + hz * dz);
        float qc = hx * hx + hz * hz - r * r;
        float t = 0.0f;

        /* a hip inside the wall: trim at the outbound crossing */
        if (qc < 0.0f && qa > 1e-6f)
            t = clampf((-qb + sqrtf(qb * qb - 4.0f * qa * qc)) / (2.0f * qa),
                       0.0f, 0.8f);
        trim[leg] = t;

        vec3 pod = {
            hip[0] + (knee[0] - hip[0]) * t,
            hip[1] + (knee[1] - hip[1]) * t,
            hip[2] + (knee[2] - hip[2]) * t,
        };
        CERR_RET_CERR(prim_emit_sphere(pod, 2.0f * cfg->leg_r[0], 6, .mesh = m));
    }
    a->rigid_nr = mesh_nr_vx(m);    /* the hull and its pods do not flex */

    for (int leg = 0; leg < ARACHNID_NR_LEGS; leg++) {
        int base = 2 + leg * ARACHNID_LEG_JOINTS;
        const float *hip = a->skel[base + 0].pos;
        const float *knee = a->skel[base + 1].pos;
        float t = trim[leg];
        vec3 start = {
            hip[0] + (knee[0] - hip[0]) * t,
            hip[1] + (knee[1] - hip[1]) * t,
            hip[2] + (knee[2] - hip[2]) * t,
        };

        /* square-section struts with ball joints at the knees */
        CERR_RET_CERR(prim_emit_sphere(a->skel[base + 1].pos, cfg->leg_r[1] * 1.6f, 6,
                                       .mesh = m));
        CERR_RET_CERR(prim_emit_prism(start, a->skel[base + 1].pos,
                                      linf_interp(cfg->leg_r[0], cfg->leg_r[1], t),
                                      cfg->leg_r[1], 4, (float)M_PI / 4.0f, .mesh = m));
        CERR_RET_CERR(prim_emit_prism(a->skel[base + 1].pos, a->skel[base + 2].pos,
                                      cfg->leg_r[1], cfg->leg_r[2], 4, (float)M_PI / 4.0f, .mesh = m));
    }

    return CERR_OK;
}

cresp(model3d) arachnid_model(arachnid *a, pipeline *pl)
{
    if (!a || !pl)
        return cresp_error(model3d, CERR_INVALID_ARGUMENTS);

    if (a->cfg.cycle == 0.0f)   arachnid_config_init(&a->cfg);

    arachnid_skeleton_build(a);

    LOCAL_SET(mesh_t, m) = CRES_RET_T(
        ref_new_checked(mesh, .name = a->name ? : "arachnid"),
        model3d
    );

    arachnid_mesh_fn mesh_fn = a->mesh ? : arachnid_mesh_organic;
    a->rigid_nr = 0;
    CERR_RET_T(mesh_fn(m, a), model3d);
    CERR_RET_T(
        skeleton_skin(.mesh = m, .joints = a->skel, .nr_joints = ARACHNID_NR_JOINTS),
        model3d
    );

    /*
     * Geometry the generator declared rigid rebinds to the root joint
     * alone: the nearest-bone skinning above weighs body verts to the
     * leg bones sweeping past them, and the legs then drag a hull out
     * of shape.
     */
    unsigned char *vx_joints = mesh_joints(m);
    float *vx_weights = mesh_weights(m);
    for (size_t v = 0; v < a->rigid_nr && v < mesh_nr_joints(m); v++) {
        memset(vx_joints + v * 4, 0, 4);
        vx_weights[v * 4 + 0] = 1.0f;
        vx_weights[v * 4 + 1] = vx_weights[v * 4 + 2] = vx_weights[v * 4 + 3] = 0.0f;
    }
    mesh_aabb_calc(m);

    return skeleton_model_new(pl, m, a->skel, ARACHNID_NR_JOINTS, a->name ? : "arachnid");
}

/* (re)entered: the speed is measured from here; the gait keeps its phase and its feet */
static void arachnid_pose_start(const void *cfg, void *priv, entity3d *e)
{
    arachnid *a = priv;

    transform_pos(&e->xform, a->prev_pos);
}

/* one frame: steer, sit the body on the ground, step, aim the legs */
static cerr arachnid_pose_eval(const void *cfg, void *priv, entity3d *e, double t, double dt_secs,
                               struct joint *out)
{
    arachnid *a = priv;
    float dt = (float)dt_secs;

    if (a->steer)   a->steer(a, t, dt);

    vec3 pos;
    transform_pos(&e->xform, pos);

    /* speed measured from the body's own horizontal motion */
    if (dt > 0.0f) {
        vec3 moved;

        vec3_sub(moved, pos, a->prev_pos);
        moved[1] = 0.0f;
        a->speed = linf_interp(a->speed, vec3_len(moved) / dt, approachf(ARA_SPEED_RATE, dt));
    }
    vec3_dup(a->prev_pos, pos);

    /* the arachnid owns the vertical: sit the body on the ground */
    pos[1] = a->ground ? a->ground(a->ground_priv, pos[0], pos[2]) : 0.0f;
    entity3d_position(e, pos);

    vec3 forward = { 0.0f, 0.0f, 1.0f }, right = { 1.0f, 0.0f, 0.0f };
    transform_rotate_vec3(&e->xform, forward);
    transform_rotate_vec3(&e->xform, right);

    cerr err = gait_update(&a->gait, pos, forward, right, a->speed, dt);
    if (IS_CERR(err))   warn_once("gait_update failed\n");

    for (int f = 0; f < ARACHNID_NR_LEGS; f++) {
        vec3 target;

        entity3d_world_to_model(e, a->feet[f].target, target);
        err = ik_chain_solve(&a->legs[f],
                             .target = { target[0], target[1], target[2] },
                             .pole   = { a->pole[f][0], a->pole[f][1], a->pole[f][2] });
        if (IS_CERR(err))   warn_once("leg IK failed\n");
    }

    return CERR_OK;
}

const anim_pose_ops arachnid_pose_ops = {
    .start  = arachnid_pose_start,
    .eval   = arachnid_pose_eval,
};

cerr arachnid_gait_init(arachnid *a)
{
    entity3d *e = a->e;

    if (!e || !e->joints || !a->skel[0].name)   return CERR_INVALID_ARGUMENTS;

    a->gait = (gait){
        .legs           = a->feet,
        .nr_legs        = ARACHNID_NR_LEGS,
        .cycle          = a->cfg.cycle,
        .swing          = a->cfg.swing,
        .stride         = a->cfg.stride,
        .step_height    = a->cfg.step_height,
        .foot_y         = a->cfg.foot_y,
        .ground         = a->ground,
        .ground_priv    = a->ground_priv,
    };

    for (int f = 0; f < ARACHNID_NR_LEGS; f++) {
        int tip = 2 + f * ARACHNID_LEG_JOINTS + 2;      /* the foot joint */

        CERR_RET(
            ik_chain_init(&a->legs[f], e, tip, ARACHNID_LEG_JOINTS),
            {
                while (f--) ik_chain_done(&a->legs[f]);
                return __cerr;
            }
        );
    }

    vec3 pos, forward = { 0.0f, 0.0f, 1.0f }, right = { 1.0f, 0.0f, 0.0f };

    transform_pos(&e->xform, pos);
    transform_rotate_vec3(&e->xform, forward);
    transform_rotate_vec3(&e->xform, right);
    gait_reset(&a->gait, pos, forward, right);

    vec3_dup(a->prev_pos, pos);
    a->speed = 0.0f;

    entity3d_pose_reset(e);
    entity3d_pose_update(e);

    return CERR_OK;
}

void arachnid_gait_done(arachnid *a)
{
    for (int f = 0; f < ARACHNID_NR_LEGS; f++)  ik_chain_done(&a->legs[f]);
}

cerr arachnid_gait_start(arachnid *a)
{
    entity3d *e = a->e;

    CERR_RET_CERR(arachnid_gait_init(a));

    animator_t *an = entity3d_animator(e);
    anim_t ani = CRES_RET(anim_lib_pose(model3d_anim_lib(e->txmodel->model), &arachnid_pose_ops, nullptr),
                          { arachnid_gait_done(a); return cerr_error_cres(__resp); });

    CERR_RET(animator_bind(an, ani, a), { arachnid_gait_done(a); return __cerr; });
    CERR_RET(animator_play(an, ani), { arachnid_gait_done(a); return __cerr; });

    return CERR_OK;
}

void arachnid_gait_stop(arachnid *a)
{
    if (!a->e)  return;

    animator_stop(entity3d_animator(a->e), 0, 0.0);
    arachnid_gait_done(a);
}
