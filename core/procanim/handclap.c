// SPDX-License-Identifier: Apache-2.0
/*
 * The hands clapped together once. The point midway between the hands is
 * where they meet; each arm (the joint its hand hangs from) swings about
 * the model's up, taken into the arm's parent frame through the pose so far,
 * by the angle that turns its hand toward that point, seen from above, in
 * as much of it as the clap's time has: fast in, slower out.
 */
#include <math.h>
#include <string.h>
#include "interp.h"
#include "procanim/handclap.h"

#define HANDCLAP_DURATION   0.45f
#define HANDCLAP_REACH      0.85f
/* HANDCLAP_MEET - of the clap's time, when the hands meet */
#define HANDCLAP_MEET       0.3f

cerr handclap_mask(handclap_t *hc, animator_t *a, model3d *m, unsigned int layer)
{
    static const joint_type hands[2] = { JOINT_HAND_LEFT, JOINT_HAND_RIGHT };

    for (int i = 0; i < 2; i++) {
        hc->hand[i] = CRES_RET(model3d_get_joint(m, hands[i]), return CERR_NOT_FOUND);
        hc->arm[i] = m->joints[hc->hand[i]].parent;
        if (hc->arm[i] < 0)     return CERR_NOT_FOUND;
    }
    hc->start = -1.0;
    hc->go = false;

    CERR_RET_CERR(animator_mask_chain(a, layer, hc->arm[0], 1));
    return animator_mask_chain(a, layer, hc->arm[1], 1);
}

/* A joint's head, model space, as posed so far */
static void handclap_head(entity3d *e, int joint, vec3 head)
{
    mat4x4 g;

    entity3d_joint_global(e, joint, g);
    vec3_dup(head, g[3]);
}

static void handclap_start(const void *_cfg, void *priv, entity3d *e)
{
    handclap_t *hc = priv;

    /* a clap asked for before the class started still goes */
    hc->start = -1.0;
}

static cerr handclap_eval(const void *_cfg, void *priv, entity3d *e, double t, double dt,
                          struct joint *out)
{
    const handclap_config *cfg = _cfg;
    handclap_t *hc = priv;
    model3d *m = e->txmodel->model;
    float duration = cfg->duration > 0.0f ? cfg->duration : HANDCLAP_DURATION;
    float reach = cfg->reach > 0.0f ? cfg->reach : HANDCLAP_REACH;

    if (hc->go) {
        hc->start = t;
        hc->go = false;
    }
    if (hc->start < 0.0)    return CERR_OK;

    float u = (float)((t - hc->start) / duration);
    if (u >= 1.0f) {
        hc->start = -1.0;
        return CERR_OK;
    }

    /* faster and faster until they meet, then easing back apart */
    float in = u / HANDCLAP_MEET;
    float s = u < HANDCLAP_MEET ? in * in : 1.0f - smoothf((u - HANDCLAP_MEET) / (1.0f - HANDCLAP_MEET));

    vec3 root[2], tip[2], mid;
    for (int i = 0; i < 2; i++) {
        handclap_head(e, hc->arm[i], root[i]);
        handclap_head(e, hc->hand[i], tip[i]);
    }
    vec3_add(mid, tip[0], tip[1]);
    vec3_scale(mid, mid, 0.5f);

    for (int i = 0; i < 2; i++) {
        /* the turn about up from the hand to the meeting point, seen from above */
        float v[2] = { tip[i][0] - root[i][0], tip[i][2] - root[i][2] };
        float w[2] = { mid[0] - root[i][0], mid[2] - root[i][2] };
        float full = atan2f(v[1] * w[0] - v[0] * w[1], v[0] * w[0] + v[1] * w[1]);

        /* the model's up, in the arm's parent frame as posed so far */
        mat4x4 parent, inv;
        vec4 up = { 0.0f, 1.0f, 0.0f, 0.0f }, axis;

        entity3d_joint_global(e, m->joints[hc->arm[i]].parent, parent);
        mat4x4_invert(inv, parent);
        mat4x4_mul_vec4(axis, inv, up);

        /* out[] is read while quat_mul() writes it */
        quat turn, posed;
        memcpy(posed, out[hc->arm[i]].rotation, sizeof(quat));
        quat_from_axis_angle(turn, axis, reach * full * s);
        quat_mul(out[hc->arm[i]].rotation, turn, posed);
    }

    return CERR_OK;
}

const anim_pose_ops handclap_pose_ops = {
    .start  = handclap_start,
    .eval   = handclap_eval,
};
