/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_TRAIL_H__
#define __CLAP_PROCANIM_TRAIL_H__

#include "animator.h"
#include "linmath.h"
#include "model.h"

/** TRAIL_MAX_JOINTS - the longest chain a trail drags */
#define TRAIL_MAX_JOINTS    48

/**
 * struct trail_config - a joint chain dragged behind: the definition's cfg
 * @joint:      the chain's first joint; negative resolves @joint_type on the
 *              model instead
 * @joint_type: the semantic joint when @joint is negative
 * @fall:       how fast a stretch of the chain off the ground falls, world
 *              units per second; 0: 4
 * @radius:     how high over the ground the chain lies: half its thickness,
 *              world units; 0 lays it right on the ground
 * @above:      the ground ray starts this far above a point, so a step up it
 *              is dragged onto is ground, not overhead; 0: 0.5
 * @depth:      ...and reaches this far below it; 0: 3
 *
 * The chain goes from @joint down its longest line of descendants, a joint's
 * head a point in the world each. The first rides the pose. Each next one
 * falls toward the ground under it and is pulled back to its bone's length
 * from the one before, so the chain drags behind the way a cord does and
 * lies where it was left. Each joint then turns to point at the next one's
 * head.
 */
typedef struct trail_config {
    int             joint;
    joint_type      joint_type;
    float           fall;
    float           radius;
    float           above;
    float           depth;
} trail_config;

/**
 * struct trail - the entity's binding
 * @joints:     the chain, from its first joint to its tip; trail_mask()
 *              finds it
 * @points:     each joint's head, world space; internal
 * @length:     the bones' lengths, world units: @points[i] to @points[i + 1];
 *              internal
 * @nr:         how many joints
 * @placed:     @points are where the chain is; until then the first eval
 *              lays them where the pose has the heads
 */
typedef struct trail {
    int             joints[TRAIL_MAX_JOINTS];
    vec3            points[TRAIL_MAX_JOINTS];
    float           length[TRAIL_MAX_JOINTS];
    unsigned int    nr;
    bool            placed;
} trail_t;

/**
 * trail_pose_ops - the trail as a pose class
 *
 * anim_lib_pose(lib, &trail_pose_ops, &cfg) on the model, animator_bind()
 * with a trail_t on the entity, played on an overlay masked to the chain
 * (trail_mask()). The ground comes from a ray cast with the entity's body
 * (phys_ray_cast()); without one, the chain hangs.
 */
extern const anim_pose_ops trail_pose_ops;

/**
 * trail_mask() - find the chain, and mask an overlay to it
 * @cfg:    the definition's configuration
 * @tr:     the binding: its chain is filled in
 * @a:      the animator
 * @m:      the model the class is defined on
 * @layer:  the overlay the class plays on
 *
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS for a model with no such joint or
 *         no bone under it, or animator_mask_chain()'s errors
 */
cerr_check trail_mask(const trail_config *cfg, trail_t *tr, animator_t *a, model3d *m, unsigned int layer);

#endif /* __CLAP_PROCANIM_TRAIL_H__ */
