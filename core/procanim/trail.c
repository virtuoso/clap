// SPDX-License-Identifier: Apache-2.0
/*
 * A joint chain dragged behind: a power cord, a tail. Its joints' heads are
 * points in the world. The first rides the pose so far; each next one falls
 * toward the ground under it, is pulled back to its bone's length from the
 * one before (nearest the first first: follow the leader), and is lifted
 * out of the ground if that put it under. Then each joint turns, in its
 * parent's frame as posed so far, from where its bone points to where the
 * next point is.
 */
#include <math.h>
#include <string.h>
#include "physics.h"
#include "util.h"
#include "procanim/trail.h"

#define TRAIL_FALL      4.0f
#define TRAIL_ABOVE     0.5f
#define TRAIL_DEPTH     3.0f

static int trail_first(const trail_config *cfg, model3d *m)
{
    if (cfg->joint >= 0)    return cfg->joint;

    cres(joint_type) jt = model3d_get_joint(m, cfg->joint_type);
    return IS_CERR(jt) ? -1 : (int)jt.val;
}

/* How many joints the longest line of descendants from @joint has, @joint's own included */
static unsigned int trail_depth(model3d *m, int joint)
{
    unsigned int deepest = 0;

    for (unsigned int j = 0; j < m->nr_joints; j++) {
        if (m->joints[j].parent != joint)   continue;

        /* once each: max() would go down twice, and twice again at every joint further down */
        unsigned int depth = trail_depth(m, j);
        deepest = max(deepest, depth);
    }

    return deepest + 1;
}

cerr trail_mask(const trail_config *cfg, trail_t *tr, animator_t *a, model3d *m, unsigned int layer)
{
    int joint = trail_first(cfg, m);

    if (joint < 0 || (unsigned int)joint >= m->nr_joints)   return CERR_INVALID_ARGUMENTS;

    tr->nr = 0;
    tr->placed = false;
    while (joint >= 0 && tr->nr < TRAIL_MAX_JOINTS) {
        int next = -1;
        unsigned int deepest = 0;

        tr->joints[tr->nr++] = joint;
        for (unsigned int j = 0; j < m->nr_joints; j++) {
            if (m->joints[j].parent != joint)   continue;

            unsigned int depth = trail_depth(m, j);
            if (depth > deepest) {
                deepest = depth;
                next = j;
            }
        }
        joint = next;
    }
    if (tr->nr < 2)     return CERR_INVALID_ARGUMENTS;

    return animator_mask_chain(a, layer, tr->joints[tr->nr - 1], tr->nr);
}

/* @g, a joint's parent's frame, becomes the joint's own: times its local translation, rotation and scale */
static void trail_frame(mat4x4 g, const struct joint *j)
{
    mat4x4 T, R;

    mat4x4_translate(T, j->translation[0], j->translation[1], j->translation[2]);
    mat4x4_mul(g, g, T);
    mat4x4_from_quat(R, (float *)j->rotation);
    mat4x4_mul(g, g, R);
    mat4x4_scale_aniso(g, g, j->scale[0], j->scale[1], j->scale[2]);
}

/* The height of the ground under @p, looked for from @above over it to @depth under it; -INFINITY for none */
static float trail_ground(entity3d *e, const vec3 p, float above, float depth)
{
    vec3 from = { p[0], p[1] + above, p[2] };
    double dist = above + depth;

    if (!phys_ray_cast(e, from, (vec3){ 0.0f, -1.0f, 0.0f }, &dist))   return -INFINITY;

    return from[1] - (float)dist;
}

static void trail_start(const void *_cfg, void *priv, entity3d *e)
{
    trail_t *tr = priv;

    tr->placed = false;
}

static cerr trail_eval(const void *_cfg, void *priv, entity3d *e, double t, double dt,
                       struct joint *out)
{
    const trail_config *cfg = _cfg;
    trail_t *tr = priv;
    model3d *m = e->txmodel->model;

    if (tr->nr < 2)     return CERR_INVALID_ARGUMENTS;

    float fall = cfg->fall > 0.0f ? cfg->fall : TRAIL_FALL;
    float above = cfg->above > 0.0f ? cfg->above : TRAIL_ABOVE;
    float depth = cfg->depth > 0.0f ? cfg->depth : TRAIL_DEPTH;

    /* the frame the chain hangs from, model to world, as posed so far */
    mat4x4 frame, g;
    entity3d_joint_global(e, m->joints[tr->joints[0]].parent, g);
    mat4x4_mul(frame, e->mx, g);

    /* the first head rides the pose; the first time, they all start out where the pose has them */
    mat4x4_dup(g, frame);
    for (unsigned int i = 0; i < (tr->placed ? 1 : tr->nr); i++) {
        struct joint *j = &out[tr->joints[i]];
        vec4 head = { j->translation[0], j->translation[1], j->translation[2], 1.0f }, at;

        mat4x4_mul_vec4(at, g, head);
        vec3_dup(tr->points[i], at);
        trail_frame(g, j);
    }
    if (!tr->placed) {
        for (unsigned int i = 0; i + 1 < tr->nr; i++)
            tr->length[i] = vec3_len((vec3){ tr->points[i + 1][0] - tr->points[i][0],
                                             tr->points[i + 1][1] - tr->points[i][1],
                                             tr->points[i + 1][2] - tr->points[i][2] });
        tr->placed = true;
    }

    /* the rest fall toward the ground and are pulled back to their bones' lengths, nearest the first first */
    for (unsigned int i = 1; i < tr->nr; i++) {
        float *p = tr->points[i];
        float ground = trail_ground(e, p, above, depth) + cfg->radius;
        vec3 d;

        p[1] = fmaxf(p[1] - fall * (float)dt, ground);

        vec3_sub(d, p, tr->points[i - 1]);
        float len = vec3_len(d);
        if (len > 0.0f) vec3_scale(d, d, tr->length[i - 1] / len);
        else            vec3_dup(d, (vec3){ 0.0f, -tr->length[i - 1], 0.0f });
        vec3_add(p, tr->points[i - 1], d);
        p[1] = fmaxf(p[1], ground);
    }

    /* each joint turns from where its bone points to the next head, both in its parent's frame */
    mat4x4_dup(g, frame);
    for (unsigned int i = 0; i + 1 < tr->nr; i++) {
        struct joint *j = &out[tr->joints[i]];
        const float *child = out[tr->joints[i + 1]].translation;
        vec4 to_w = {
            tr->points[i + 1][0] - tr->points[i][0],
            tr->points[i + 1][1] - tr->points[i][1],
            tr->points[i + 1][2] - tr->points[i][2],
            0.0f,
        }, to;
        vec3 bone, scaled = { child[0] * j->scale[0], child[1] * j->scale[1], child[2] * j->scale[2] }, axis;
        mat4x4 inv;

        mat4x4_invert(inv, g);
        mat4x4_mul_vec4(to, inv, to_w);
        quat_mul_vec3(bone, j->rotation, scaled);
        vec3_mul_cross(axis, bone, to);

        /* j->rotation is read while quat_mul() writes it */
        quat turn, posed;
        memcpy(posed, j->rotation, sizeof(quat));
        quat_from_axis_angle(turn, axis, atan2f(vec3_len(axis), vec3_mul_inner(bone, to)));
        quat_mul(j->rotation, turn, posed);

        trail_frame(g, j);
    }

    return CERR_OK;
}

const anim_pose_ops trail_pose_ops = {
    .start  = trail_start,
    .eval   = trail_eval,
};
