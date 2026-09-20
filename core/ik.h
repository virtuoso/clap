/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_IK_H__
#define __CLAP_IK_H__

#include "error.h"
#include "linmath.h"
#include "model.h"

/**
 * struct ik_chain - a FABRIK-driven run of an entity's skeleton joints
 * @e:              the entity whose skeleton the chain drives; the chain
 *                  doesn't own it, keep it alive
 * @joints:         model joint indices, chain root to tip
 * @points:         working joint positions, model space
 * @lengths:        segment rest lengths
 * @rest_points:    bind pose joint positions, model space
 * @rest_dir:       unit directions to the next joint, in each joint's
 *                  rotated frame
 * @rest_rot:       local bind rotations
 * @rest_trans:     local bind translations
 * @rest_scale:     local bind scales
 * @base_rot:       bind orientation of the chain root's parent
 * @nr:             number of joints in the chain
 *
 * Set up with ik_chain_init(), driven with ik_chain_solve(), torn down
 * with ik_chain_done(). All fields are the chain's own; don't modify.
 */
typedef struct ik_chain {
    entity3d    *e;
    int         *joints;
    vec3        *points;
    float       *lengths;
    vec3        *rest_points;
    vec3        *rest_dir;
    quat        *rest_rot;
    vec3        *rest_trans;
    vec3        *rest_scale;
    quat        base_rot;
    size_t      nr;
} ik_chain;

/**
 * ik_chain_init() - bind an IK chain to a run of an entity's joints
 * @chain:  the chain, in any state; fully overwritten
 * @e:      entity with an armature
 * @tip:    model joint index of the chain's end effector
 * @nr:     chain length in joints, tip and root included, at least 2
 *
 * Walk @nr - 1 steps up the skeleton from @tip and bind the chain to the
 * joints along the way, capturing their bind pose as the rest
 * configuration. The chain assumes the joints above its root sit at bind
 * pose.
 * Context: anywhere
 * Return: CERR_OK, CERR_NOMEM or CERR_INVALID_ARGUMENTS (no armature,
 * bad @tip, walking past the skeleton root, zero-length segments).
 */
cerr_check ik_chain_init(ik_chain *chain, entity3d *e, int tip, size_t nr);

/**
 * ik_chain_done() - free everything an IK chain holds
 * @chain:  the chain; safe to call on a zeroed or partially built one
 */
void ik_chain_done(ik_chain *chain);

/**
 * struct ik_solve_opts - per-solve IK parameters
 * @target:         end effector goal, in the entity's model space
 * @pole:           bend side hint (see &struct fabrik_solve_opts); also
 *                  defines the bend plane that the joints' roll is
 *                  anchored to
 * @max_iterations: FABRIK iteration cap; 0 picks the default
 * @tolerance:      acceptable end effector miss; 0 picks the default
 */
typedef struct ik_solve_opts {
    vec3            target;
    vec3            pole;
    unsigned int    max_iterations;
    float           tolerance;
} ik_solve_opts;

cerr_check _ik_chain_solve(ik_chain *chain, const ik_solve_opts *opts);

/**
 * ik_chain_solve() - aim an IK chain's end effector at a target
 * @_chain: the chain
 * @args:   &struct ik_solve_opts field initializers
 *
 * Solve the chain from its rest configuration with FABRIK and rotate
 * every joint so its bone tracks the solved segment. Local rotations
 * only: the caller recomputes the skeleton (entity3d_pose_update())
 * once all of the frame's pose writers are done, which for a pose class
 * (core/animator.h) the animator does. With a pole, every
 * joint's roll is anchored to the bend plane (spanned by the
 * root-to-target line and the pole), making the roll a function of the
 * current pose alone; without one, rotations are minimal arcs from the
 * parent, i.e. parallel transport, whose roll follows the path the
 * chain took and drifts under cyclic motion. The joints' local
 * rotations are replaced wholesale, so the chain doesn't layer on top
 * of a playing animation.
 * Context: anywhere between the frame's pose reset and update
 * Return: CERR_OK or CERR_INVALID_ARGUMENTS
 */
#define ik_chain_solve(_chain, args...) _ik_chain_solve((_chain), &(ik_solve_opts){ args })

#endif /* __CLAP_IK_H__ */
