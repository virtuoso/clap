/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_REACH_H__
#define __CLAP_PROCANIM_REACH_H__

#include "animator.h"
#include "ik.h"
#include "linmath.h"
#include "procanim/target.h"

/**
 * struct reach_config - an IK chain's tip at a point: the definition's cfg
 * @pole:       the chain's bend side hint (ik_solve_opts::pole)
 * @tolerance:  how close the tip has to land (ik_solve_opts::tolerance);
 *              0 is the solver's default, a fraction of the chain's length
 * @max_iterations: the solver's iteration cap; 0 is its default. A folded
 *              chain (a kneeling leg) converges slowly
 * @law:        how the aim point moves; a MOVE or HOLD ends the play at
 *              its duration, so a sequence of reaches chains
 */
typedef struct reach_config {
    target_law      law;
    vec3            pole;
    float           tolerance;
    unsigned int    max_iterations;
} reach_config;

/**
 * struct reach - the entity's binding
 * @chain:  the chain to solve, initialized by the binder (ik_chain_init())
 *          and owned by it
 * @target: what to reach for; the brain writes it. One binding serves
 *          every reach definition on the entity: the aim point carries
 *          over from one law to the next
 */
typedef struct reach {
    ik_chain        *chain;
    target_t        target;
} reach_t;

/**
 * reach_pose_ops - the reach as a pose class
 *
 * anim_lib_pose(lib, &reach_pose_ops, &cfg) per law on the model,
 * animator_bind() each with the one reach_t on the entity. eval solves
 * the chain onto the aim point. On the base layer from the bind pose, as
 * the tentacle does; on an overlay from the pose so far
 * (ik_chain_rest_from_pose()) into the layer's buffer, so it layers over
 * a playing clip: a TARGET_PLANT law keeps a foot on the ground under it
 * while the clip walks. It honours the animator's pins (animator_pin()):
 * the chain up to the pinned joint furthest from the root stays where the
 * pinned pose has it.
 */
extern const anim_pose_ops reach_pose_ops;

#endif /* __CLAP_PROCANIM_REACH_H__ */
