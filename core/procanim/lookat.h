/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_LOOKAT_H__
#define __CLAP_PROCANIM_LOOKAT_H__

#include "animator.h"
#include "linmath.h"
#include "model.h"
#include "procanim/target.h"

/**
 * struct lookat_config - one joint aimed at a point: the definition's cfg
 * @joint:      the aimed joint; negative resolves @joint_type on the model
 *              instead, so one configuration serves every model with,
 *              say, a head
 * @joint_type: the semantic joint when @joint is negative
 * @forward:    the joint's forward axis in its own frame; zero takes the
 *              bind pose's, where the model faces +Z
 * @up:         the joint's up axis in its own frame; zero takes the bind
 *              pose's, where up is +Y
 * @yaw_max:    how far from the posed forward it turns sideways, radians;
 *              0 is unlimited
 * @pitch_max:  the same up and down
 * @slew:       turning rate, rad/s; 0 is unlimited
 * @law:        how the aim point moves
 *
 * The rotation is a delta over the joint's posed rotation, so the class
 * overlays a clip: the head turns from wherever the clip has it. With
 * target::none set the joint turns back to the pose.
 */
typedef struct lookat_config {
    int             joint;
    joint_type      joint_type;
    vec3            forward;
    vec3            up;
    double          yaw_max;
    double          pitch_max;
    double          slew;
    target_law      law;
} lookat_config;

/**
 * struct lookat - the entity's binding
 * @target:     what to aim at; the brain writes it
 * @yaw:        the current turn, radians; internal
 * @pitch:      the same up and down
 */
typedef struct lookat {
    target_t        target;
    double          yaw;
    double          pitch;
} lookat_t;

/**
 * lookat_pose_ops - the look-at as a pose class
 *
 * anim_lib_pose(lib, &lookat_pose_ops, &cfg) on the model, animator_bind()
 * with a lookat_t on the entity, played on an overlay masked to the joint
 * (animator_mask_subtree()). eval writes the joint's rotation and nothing
 * else.
 */
extern const anim_pose_ops lookat_pose_ops;

#endif /* __CLAP_PROCANIM_LOOKAT_H__ */
