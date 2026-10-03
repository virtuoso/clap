// SPDX-License-Identifier: Apache-2.0
/*
 * Feet planted on uneven ground under a playing clip. The clip walks on a
 * flat floor at the model's origin; here each foot is set onto the ground
 * actually under it, and the pelvis follows the lowest foot on the floor:
 * down, so the foot on the lower ground can reach, and up, so a character
 * whose origin sits below the ground stands on it instead of crouching in
 * it. The feet's targets are read before anything moves, the pelvis
 * moves, then each leg is re-rested from the moved pose and solved onto
 * its target: one class, so the order holds.
 * Lengths are measured in legs, so a model in any unit under any entity
 * scale plants the same.
 */
#include <math.h>
#include "interp.h"
#include "model.h"
#include "physics.h"
#include "util.h"
#include "procanim/plant.h"

/* the defaults, in leg lengths */
#define PLANT_TOLERANCE 0.001f
#define PLANT_ABOVE     0.3f
#define PLANT_DEPTH     1.5f
#define PLANT_MAX_DROP  0.45f
#define PLANT_STANCE    0.25f
#define PLANT_KNEE_BIAS 0.1f

/* a leg's length, hip to foot, model units */
static float leg_length(const ik_chain *c)
{
    float len = 0.0f;

    for (size_t k = 0; k + 1 < c->nr; k++)  len += c->lengths[k];

    return len;
}

/*
 * The ground under a model-space point: its height over the clip's floor,
 * false for none found. @leg is the leg's length in the world, which the
 * ray's reach is measured in.
 */
static bool plant_ground(const plant_config *cfg, entity3d *e, const vec3 point, float leg, float *ground)
{
    float above = (cfg->above > 0.0f ? cfg->above : PLANT_ABOVE) * leg;
    float depth = (cfg->depth > 0.0f ? cfg->depth : PLANT_DEPTH) * leg;
    vec4 p = { point[0], point[1], point[2], 1.0f }, world;

    if (!e->phys_body)  return false;

    mat4x4_mul_vec4(world, e->mx, p);

    vec3 start = { world[0], world[1] + above, world[2] }, down = { 0.0f, -1.0f, 0.0f };
    double dist = above + depth;

    if (!phys_ray_cast(e, start, down, &dist))  return false;

    vec3 hit = { start[0], start[1] - (float)dist, start[2] }, model;

    entity3d_world_to_model(e, hit, model);
    *ground = model[1];

    return true;
}

static cerr plant_eval(const void *_cfg, void *priv, entity3d *e, double t, double dt,
                       struct joint *out)
{
    const plant_config *cfg = _cfg;
    plant_t *p = priv;
    model3d *m = e->txmodel->model;
    vec3 target[PLANT_MAX_FEET];
    float lowest = 0.0f;
    bool standing = false;

    if (!p->nr || p->nr > PLANT_MAX_FEET || p->pelvis < 0 || (unsigned int)p->pelvis >= m->nr_joints)
        return CERR_INVALID_ARGUMENTS;

    /* where the clip puts each foot, and the ground under it, before anything moves */
    float stance = cfg->stance > 0.0f ? cfg->stance : PLANT_STANCE;
    float scale = e->scale != 0.0f ? e->scale : 1.0f;
    float leg[PLANT_MAX_FEET], longest = 0.0f;

    for (unsigned int i = 0; i < p->nr; i++) {
        ik_chain *c = p->chain[i];

        if (!c)     return CERR_INVALID_ARGUMENTS;

        leg[i] = leg_length(c);
        longest = max(longest, leg[i]);

        ik_chain_point(c, out, c->nr - 1, target[i]);
        if (!plant_ground(cfg, e, target[i], leg[i] * scale, &p->ground[i]))    p->ground[i] = 0.0f;

        /* a foot in the air follows the ground but moves no pelvis */
        if (target[i][1] <= stance * leg[i]) {
            lowest = standing ? min(lowest, p->ground[i]) : p->ground[i];
            standing = true;
        }
        target[i][1] += p->ground[i];
    }

    /*
     * The pelvis follows the lowest foot on the floor, down or up: with
     * every such foot's ground above the clip's floor the character is
     * sunk and rises by the least of them. With no foot on the floor (a
     * flight phase) it stays where it is rather than fall back and pop.
     */
    float want = standing ? max(lowest, -(cfg->max_drop > 0.0f ? cfg->max_drop : PLANT_MAX_DROP) * longest)
                          : p->drop;

    p->drop = cfg->drop_rate > 0.0 ? linf_interp(p->drop, want, approachf((float)cfg->drop_rate, (float)dt))
                                   : want;

    if (p->drop != 0.0f) {
        /* a model-space drop in the pelvis's local translation: through its parent's frame */
        mat4x4 parent, inv;
        vec4 d = { 0.0f, p->drop, 0.0f, 0.0f }, local;

        entity3d_joint_global(e, m->joints[p->pelvis].parent, parent);
        mat4x4_invert(inv, parent);
        mat4x4_mul_vec4(local, inv, d);
        vec3_add(out[p->pelvis].translation, out[p->pelvis].translation, local);
    }

    /* each leg from the dropped pose onto its ground */
    float bias = cfg->knee_bias > 0.0f ? cfg->knee_bias : PLANT_KNEE_BIAS;
    vec3 forward = { 0.0f, 0.0f, 1.0f };

    if (vec3_mul_inner(cfg->forward, cfg->forward) > 0.0f)  vec3_norm(forward, (float *)cfg->forward);

    for (unsigned int i = 0; i < p->nr; i++) {
        ik_chain *c = p->chain[i];

        CERR_RET_CERR(ik_chain_rest_from_pose(c, out));

        /*
         * The pole: the clip's knee off the hip-to-foot line, in model
         * units, plus forward at a fraction of the leg. A real bend
         * outweighs the bias and is kept; a straight leg's is noise next
         * to it and the knee goes forward.
         */
        vec3 axis, knee, off, pole;

        vec3_sub(axis, c->rest_points[c->nr - 1], c->rest_points[0]);
        vec3_norm_safe(axis, axis);
        vec3_sub(knee, c->rest_points[1], c->rest_points[0]);
        vec3_add_scaled(off, knee, axis, 1.0f, -vec3_mul_inner(knee, axis));
        vec3_add_scaled(pole, off, forward, 1.0f, bias * leg[i]);

        CERR_RET_CERR(ik_chain_solve(c,
                                     .target         = { target[i][0], target[i][1], target[i][2] },
                                     .pole           = { pole[0], pole[1], pole[2] },
                                     .tolerance      = (cfg->tolerance > 0.0f ? cfg->tolerance : PLANT_TOLERANCE) * leg[i],
                                     .max_iterations = cfg->max_iterations,
                                     .out            = out));
    }

    return CERR_OK;
}

const anim_pose_ops plant_pose_ops = {
    .eval   = plant_eval,
};

cerr plant_mask(plant_t *p, animator_t *a, unsigned int layer)
{
    CERR_RET_CERR(animator_mask_chain(a, layer, p->pelvis, 1));

    for (unsigned int i = 0; i < p->nr; i++) {
        ik_chain *c = p->chain[i];

        if (!c)     return CERR_INVALID_ARGUMENTS;
        CERR_RET_CERR(animator_mask_chain(a, layer, c->joints[c->nr - 1], c->nr));
    }

    return CERR_OK;
}
