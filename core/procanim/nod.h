/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_NOD_H__
#define __CLAP_PROCANIM_NOD_H__

#include "animator.h"
#include "linmath.h"
#include "model.h"

/**
 * struct nod_config - a joint nodding as the entity goes: the definition's cfg
 * @joint:      the nodding joint; negative resolves @joint_type on the model
 *              instead
 * @joint_type: the semantic joint when @joint is negative
 * @angle:      how far it nods either way at full speed, radians
 * @frequency:  nods per second at full speed; slower, it nods slower
 * @rate:       how fast the nod's size follows nod::amount, 1/s; 0 snaps
 *
 * The joint turns about the model's sideways axis (+X), as its parent is
 * posed so far, along a sine: a delta over the posed rotation, as wide and
 * as quick as nod::amount makes it, so it settles when the entity stops.
 */
typedef struct nod_config {
    int             joint;
    joint_type      joint_type;
    float           angle;
    float           frequency;
    float           rate;
} nod_config;

/**
 * struct nod - the entity's binding
 * @amount: how much the entity goes, 0 standing to 1 at full speed; the
 *          brain writes it
 * @size:   how much of the nod it has, following @amount; internal
 * @phase:  where in a nod it is, radians; internal
 */
typedef struct nod {
    float   amount;
    float   size;
    float   phase;
} nod_t;

/**
 * nod_pose_ops - the nod as a pose class
 *
 * anim_lib_pose(lib, &nod_pose_ops, &cfg) on the model, animator_bind() with
 * a nod_t on the entity, played on an overlay masked to the joint
 * (nod_mask()), over the ones it nods on top of. eval writes the joint's
 * rotation and nothing else.
 */
extern const anim_pose_ops nod_pose_ops;

/**
 * nod_mask() - mask an overlay to what the class writes
 * @cfg:    the definition's configuration
 * @a:      the animator
 * @m:      the model the class is defined on
 * @layer:  the overlay the class plays on
 *
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS for a model with no such joint, or
 *         animator_mask_chain()'s errors
 */
cerr_check nod_mask(const nod_config *cfg, animator_t *a, model3d *m, unsigned int layer);

#endif /* __CLAP_PROCANIM_NOD_H__ */
