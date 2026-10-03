/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_PLANT_H__
#define __CLAP_PROCANIM_PLANT_H__

#include "animator.h"
#include "ik.h"
#include "linmath.h"

#define PLANT_MAX_FEET  4

/**
 * struct plant_config - feet planted on the ground under them: the definition's cfg
 * @tolerance:      how close a foot has to land; 0: 0.001
 * @max_iterations: the solver's iteration cap for a leg of more than two
 *                  bones; 0 is its default
 * @above:          the ground ray starts this far above the foot, which
 *                  makes it the tallest step a foot climbs onto: anything
 *                  higher (the seat a character is getting up from, a
 *                  ledge it walks under) is overhead, not ground; 0: 0.3
 * @depth:          ...and reaches this far below it; 0: 1.5
 * @drop_rate:      how fast the pelvis follows the lowest foot, 1/s; 0 snaps
 * @max_drop:       how far the pelvis may drop; 0: 0.45. A rise needs no
 *                  cap: the ground ray finds nothing higher than @above
 * @stance:         a foot the clip has higher than this over its floor is
 *                  in the air: it still follows the ground, but does not
 *                  pull the pelvis down; 0: 0.25
 * @forward:        which way the knees bend, model space; zero is +Z
 * @knee_bias:      how much of that goes into the pole against the bend
 *                  the clip's own knee shows; 0: 0.1
 *
 * Every length is in leg lengths (the chain's, hip to foot), not in the
 * model's units: models come in any unit under any entity scale, and a
 * giant's ankle is as high over its floor as a mouse's, in legs.
 *
 * Over a playing clip, each foot goes where the clip has it, set onto the
 * ground found under it, and the pelvis follows the lowest foot the clip
 * has on the floor (the model's origin plane): down where that foot's
 * ground is lower, so it can reach and the others bend; up where every
 * such foot's ground is higher, so a character whose origin sits below
 * the ground stands on it instead of crouching in it. With no foot on
 * the floor the pelvis stays where it is. The knee goes the way the clip's
 * own knee points off the hip-to-foot line plus @forward: a clearly bent
 * clip leg keeps its bend, a nearly straight one, whose bend direction is
 * noise, bends forward, and neither flips from frame to frame.
 */
typedef struct plant_config {
    float           tolerance;
    unsigned int    max_iterations;
    float           above;
    float           depth;
    double          drop_rate;
    float           max_drop;
    float           stance;
    vec3            forward;
    float           knee_bias;
} plant_config;

/**
 * struct plant - the entity's binding
 * @pelvis:     the joint that drops: the legs' common ancestor (the hips)
 * @chain:      the leg chains, foot at the tip, initialized by the binder
 *              (ik_chain_init()) and owned by it
 * @nr:         how many feet
 * @drop:       the pelvis's current offset, model units, negative down,
 *              positive up; internal
 * @ground:     each foot's ground as of the last eval, model units above
 *              the clip's floor; for a brain that wants to know
 */
typedef struct plant {
    ik_chain        *chain[PLANT_MAX_FEET];
    unsigned int    nr;
    int             pelvis;
    float           drop;
    float           ground[PLANT_MAX_FEET];
} plant_t;

/**
 * plant_pose_ops - planted feet as a pose class
 *
 * anim_lib_pose(lib, &plant_pose_ops, &cfg) on the model, animator_bind()
 * with a plant_t on the entity, played on an overlay masked to the pelvis
 * and the leg chains (plant_mask()). eval reads each foot's ground from
 * the pose so far, drops the pelvis, then solves each leg from the
 * dropped pose onto its ground. The ground comes from a ray cast with the
 * entity's body (phys_ray_cast()); without one the feet stay where the
 * clip has them.
 */
extern const anim_pose_ops plant_pose_ops;

/**
 * plant_mask() - mask an overlay to what the class writes
 * @p:      the binding, with plant::pelvis and the chains filled
 * @a:      the animator
 * @layer:  the overlay the class plays on
 *
 * Return: CERR_OK or animator_mask_chain()'s errors
 */
cerr_check plant_mask(plant_t *p, animator_t *a, unsigned int layer);

#endif /* __CLAP_PROCANIM_PLANT_H__ */
