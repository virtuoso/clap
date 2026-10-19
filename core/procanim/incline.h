/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_INCLINE_H__
#define __CLAP_PROCANIM_INCLINE_H__

#include "animator.h"
#include "linmath.h"
#include "model.h"

/**
 * struct incline_config - a joint tilted with the ground: the definition's cfg
 * @joint:  the tilted joint; negative takes the skeleton's root
 * @rate:   how fast the tilt follows the normal, 1/s (vec3_approach()); 0
 *          snaps
 *
 * The joint turns so that the world's up points along the normal, a delta
 * over the joint's posed rotation about the joint's origin: a skeleton
 * rooted where the model meets the floor tilts about the point under it,
 * onto the incline it stands on.
 */
typedef struct incline_config {
    int     joint;
    float   rate;
} incline_config;

/**
 * struct incline - the entity's binding
 * @normal: what the model's up follows, world space, unit length; the brain
 *          writes it, the vertical for upright
 * @up:     the model's up as it stands, world space; internal
 */
typedef struct incline {
    vec3    normal;
    vec3    up;
} incline_t;

/**
 * incline_pose_ops - the incline as a pose class
 *
 * anim_lib_pose(lib, &incline_pose_ops, &cfg) on the model, animator_bind()
 * with an incline_t on the entity, played on an overlay masked to the joint
 * (incline_mask()), below the overlays that aim or plant from the posed
 * skeleton. eval writes the joint's rotation and nothing else.
 */
extern const anim_pose_ops incline_pose_ops;

/**
 * incline_mask() - mask an overlay to what the class writes
 * @cfg:    the definition's configuration
 * @a:      the animator
 * @m:      the model the class is defined on
 * @layer:  the overlay the class plays on
 *
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS for a model with no such joint, or
 *         animator_mask_chain()'s errors
 */
cerr_check incline_mask(const incline_config *cfg, animator_t *a, model3d *m, unsigned int layer);

#endif /* __CLAP_PROCANIM_INCLINE_H__ */
