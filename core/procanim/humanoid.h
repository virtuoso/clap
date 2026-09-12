/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_HUMANOID_H__
#define __CLAP_PROCANIM_HUMANOID_H__

#include "error.h"
#include "linmath.h"
#include "model.h"
#include "ik.h"
#include "procanim/gait.h"

struct scene;

/**
 * struct gait_config - a biped's walk, fully parameterized
 * @cycle:          seconds per gait cycle
 * @swing:          fraction of the cycle each foot is airborne
 * @stride:         distance between successive footfalls of one leg
 * @step_height:    swing arc height
 * @foot_y:         planted foot joint height above the ground
 * @hip_x:          lateral foot offset from the body center line
 * @base_y:         body height offset (crouch keeps the knees bent and
 *                  the feet in comfortable IK reach)
 * @bob:            body bob amplitude, two bobs per cycle
 * @arm_swing:      arm swing amplitude, radians; 0 disables
 * @knee_dir:       knee bend direction (IK pole), model space
 * @shoulders:      arm swing joint names; nullptr entries skip that arm
 * @leg_joints:     joints per leg chain, tip included
 *
 * gait_config_init() writes out the reference tuning; start from it and
 * override per character.
 */
typedef struct gait_config {
    float           cycle;
    float           swing;
    float           stride;
    float           step_height;
    float           foot_y;
    float           hip_x;
    float           base_y;
    float           bob;
    float           arm_swing;
    vec3            knee_dir;
    const char      *shoulders[2];
    unsigned int    leg_joints;
} gait_config;

/**
 * gait_config_init() - fill a gait configuration with the defaults
 * @cfg:    the configuration; fully overwritten
 */
void gait_config_init(gait_config *cfg);

typedef struct humanoid humanoid;

/**
 * typedef humanoid_steer - body driver callback
 * @h:  the humanoid
 * @t:  animation time, seconds, anchored at gait start
 * @dt: seconds since the previous update
 *
 * Called at the top of every update: drive the entity's position and
 * heading (entity3d_position(), entity3d_rotate()) however the
 * character moves. The humanoid owns the vertical: it overwrites the
 * entity's Y with gait_config::base_y plus bob. Speed is measured from
 * the entity's horizontal motion, so steering doesn't report it.
 */
typedef void (*humanoid_steer)(humanoid *h, double t, float dt);

/**
 * struct humanoid - a biped walking on IK legs
 * @scene:      the scene, for the animation clock (required)
 * @e:          the entity to drive (required); the humanoid does not
 *              own it and claims its update callback and priv pointer
 * @steer:      body driver; nullptr walks in place
 * @steer_priv: the steer callback's own state
 * @cfg:        the walk parameters; zero cycle picks the defaults
 *
 * The rest is internal: leg IK chains, gait state, measured speed.
 * humanoid_gait_start() resolves the legs through the semantic
 * JOINT_FOOT_LEFT/JOINT_FOOT_RIGHT joint types, the arms through
 * gait_config::shoulders names, installs the update callback and
 * starts walking; humanoid_gait_stop() restores everything.
 */
struct humanoid {
    struct scene    *scene;
    entity3d        *e;
    humanoid_steer  steer;
    void            *steer_priv;
    gait_config     cfg;

    ik_chain        legs[2];
    gait_leg        feet[2];
    gait            gait;
    int             shoulders[2];
    int             (*orig_update)(entity3d *, void *);
    vec3            prev_pos;
    float           speed;
    double          t0;
    double          t_last;
};

/**
 * humanoid_gait_start() - put a biped on its feet
 * @h:  the humanoid, with humanoid::scene, humanoid::e and optionally
 *      humanoid::cfg and humanoid::steer filled
 *
 * Context: entity setup time
 * Return: CERR_OK, CERR_NOMEM, CERR_INVALID_ARGUMENTS (no skeleton, no
 * foot joints, bad chain) or CERR_INVALID_OPERATION (the entity's priv
 * pointer is taken)
 */
cerr_check humanoid_gait_start(humanoid *h);

/**
 * humanoid_gait_stop() - stop walking and release the entity
 * @h:  a started humanoid; the entity's update and priv are restored
 *      and the leg chains freed
 */
void humanoid_gait_stop(humanoid *h);

#endif /* __CLAP_PROCANIM_HUMANOID_H__ */
