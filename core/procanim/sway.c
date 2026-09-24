// SPDX-License-Identifier: Apache-2.0
/*
 * Wind sway for joint chains: layered sines (base sway, gust harmonic,
 * tip-weighted flutter, twist), phase-lagged along the chain.
 * chain_sway() writes local joint rotations between the caller's pose
 * reset and update; sway_pose_ops is it as a pose class (core/animator.h)
 * over a sway_rig binding, and sway_rig_start() plays that on an entity.
 */
#include <math.h>
#include "common.h"
#include "animator.h"
#include "sway.h"

#define SWAY_AMPLITUDE      0.122f  /* 7 degrees */
#define SWAY_AMP_POWER      1.6f
#define SWAY_FREQ           0.9f
#define SWAY_LAG            0.45f
#define SWAY_GUST           0.45f
#define SWAY_GUST_FREQ      2.17f
#define SWAY_GUST_LAG       0.8f
#define SWAY_GUST_PHASE     1.3f
#define SWAY_FLUTTER        0.25f
#define SWAY_FLUTTER_FREQ   6.8f
#define SWAY_FLUTTER_LAG    -2.1f
#define SWAY_TWIST          0.36f
#define SWAY_TWIST_FREQ     1.4f
#define SWAY_TWIST_LAG      0.9f

void sway_config_init(sway_config *cfg)
{
    *cfg = (sway_config){
        .wind               = { 1.0f, 0.0f, 0.0f },
        .amplitude          = SWAY_AMPLITUDE,
        .amp_power          = SWAY_AMP_POWER,
        .frequency          = SWAY_FREQ,
        .phase_lag          = SWAY_LAG,
        .gust               = SWAY_GUST,
        .gust_frequency     = SWAY_GUST_FREQ,
        .gust_lag           = SWAY_GUST_LAG,
        .flutter            = SWAY_FLUTTER,
        .flutter_frequency  = SWAY_FLUTTER_FREQ,
        .flutter_lag        = SWAY_FLUTTER_LAG,
        .twist              = SWAY_TWIST,
        .twist_frequency    = SWAY_TWIST_FREQ,
        .twist_lag          = SWAY_TWIST_LAG,
    };
}

cerr chain_sway(entity3d *e, double t, const sway_config *cfg)
{
    if (!e || !e->joints || !cfg || cfg->amplitude == 0.0f) return CERR_INVALID_ARGUMENTS;

    unsigned int nr_joints = e->txmodel->model->nr_joints;
    unsigned int first = cfg->first;
    unsigned int nr = cfg->nr ? : nr_joints - first;

    if (first >= nr_joints || first + nr > nr_joints)   return CERR_INVALID_ARGUMENTS;

    vec3 wind;
    vec3_dup(wind, cfg->wind);
    wind[1] = 0.0f;
    if (vec3_len(wind) == 0.0f) vec3_dup(wind, (vec3){ 1.0f, 0.0f, 0.0f });
    vec3_norm(wind, wind);

    vec3 bend_axis = { -wind[2], 0.0f, wind[0] };

    float amp_power = cfg->amp_power ? : SWAY_AMP_POWER;
    float freq = cfg->frequency ? : SWAY_FREQ;
    float lag = cfg->phase_lag ? : SWAY_LAG;
    float gust = cfg->no_gust ? 0.0f : (cfg->gust ? : SWAY_GUST);
    float gust_freq = cfg->gust_frequency ? : SWAY_GUST_FREQ;
    float gust_lag = cfg->gust_lag ? : SWAY_GUST_LAG;
    float flutter = cfg->no_flutter ? 0.0f : (cfg->flutter ? : SWAY_FLUTTER);
    float flutter_freq = cfg->flutter_frequency ? : SWAY_FLUTTER_FREQ;
    float flutter_lag = cfg->flutter_lag ? : SWAY_FLUTTER_LAG;
    float twist = cfg->no_twist ? 0.0f : (cfg->twist ? : SWAY_TWIST);
    float twist_freq = cfg->twist_frequency ? : SWAY_TWIST_FREQ;
    float twist_lag = cfg->twist_lag ? : SWAY_TWIST_LAG;
    float tf = (float)(t + cfg->phase);

    for (unsigned int i = 0; i < nr; i++) {
        unsigned int joint = first + i;
        float f = (float)(i + 1) / nr;
        float amp = cfg->amplitude * powf(f, amp_power);

        float bend = sinf(tf * freq - (i + 1) * lag)
                   + gust * sinf(tf * gust_freq - (i + 1) * gust_lag + SWAY_GUST_PHASE)
                   + flutter * f * f * sinf(tf * flutter_freq - (i + 1) * flutter_lag);
        float yaw = cfg->amplitude * twist * f * sinf(tf * twist_freq + (i + 1) * twist_lag);

        quat qbend, qtwist;
        quat_from_axis_angle(qbend, bend_axis, amp * bend);
        quat_from_axis_angle(qtwist, (vec3){ 0.0f, 1.0f, 0.0f }, yaw);
        quat_mul(e->joints[joint].rotation, qbend, qtwist);
    }

    return CERR_OK;
}

static cerr sway_pose_eval(const void *cfg, void *priv, entity3d *e, double t, double dt,
                           struct joint *out)
{
    sway_rig *rig = priv;

    return chain_sway(e, t, &rig->cfg);
}

const anim_pose_ops sway_pose_ops = {
    .eval   = sway_pose_eval,
};

cerr sway_rig_start(sway_rig *rig)
{
    entity3d *e = rig->e;

    if (!e || !e->joints)   return CERR_INVALID_ARGUMENTS;

    if (rig->cfg.amplitude == 0.0f) sway_config_init(&rig->cfg);

    animator_t *a = entity3d_animator(e);
    anim_t ani = CRES_RET_CERR(anim_lib_pose(model3d_anim_lib(e->txmodel->model), &sway_pose_ops, nullptr));

    CERR_RET_CERR(animator_bind(a, ani, rig));

    return animator_play(a, ani);
}

void sway_rig_stop(sway_rig *rig)
{
    if (!rig->e)    return;

    animator_stop(entity3d_animator(rig->e), 0, 0.0);
}
