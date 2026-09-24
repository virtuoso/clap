/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_TURRET_H__
#define __CLAP_PROCANIM_TURRET_H__

#include "animator.h"
#include "linmath.h"
#include "procanim/target.h"

/**
 * struct turret_config - a yaw joint and a pitch joint aimed at a point
 * @yaw_joint:      turns around its own up (+Y) axis
 * @pitch_joint:    a child of @yaw_joint, turns around its own right (+X)
 *                  axis; its forward (+Z) is the barrel
 * @yaw_min:        yaw range, radians from the posed forward; both 0 is
 *                  unlimited, and the turret takes the short way around
 * @yaw_max:        ...
 * @pitch_min:      pitch range, radians, positive up; both 0 is unlimited
 * @pitch_max:      ...
 * @slew:           turning rate of each joint, rad/s; 0 is unlimited
 * @law:            how the aim point moves
 *
 * Like lookat_config, the rotations are deltas over the posed ones. With
 * target::none set both joints turn back to the pose.
 */
typedef struct turret_config {
    int             yaw_joint;
    int             pitch_joint;
    double          yaw_min;
    double          yaw_max;
    double          pitch_min;
    double          pitch_max;
    double          slew;
    target_law      law;
} turret_config;

/**
 * struct turret - the entity's binding
 * @target:     what to aim at; the brain writes it
 * @yaw:        the current turn of each joint, radians; internal
 * @pitch:      ...
 * @yaw_err:    what was left to turn as of the last eval: how far off the
 *              aim is, for a brain that fires once settled
 * @pitch_err:  ...
 */
typedef struct turret {
    target_t        target;
    double          yaw;
    double          pitch;
    double          yaw_err;
    double          pitch_err;
} turret_t;

/**
 * turret_pose_ops - the turret as a pose class
 *
 * anim_lib_pose(lib, &turret_pose_ops, &cfg) on the model, animator_bind()
 * with a turret_t on the entity, played on an overlay masked to the yaw
 * joint's subtree. eval writes the two joints' rotations and nothing else.
 */
extern const anim_pose_ops turret_pose_ops;

#endif /* __CLAP_PROCANIM_TURRET_H__ */
