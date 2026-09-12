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
 * @lean:           forward body lean, radians, spread over the spine
 * @speed:          the body speed this tuning is authored for; the
 *                  humanoid blends its walk and run configurations by
 *                  where the measured speed falls between their speeds
 * @knee_dir:       knee bend direction (IK pole), model space
 * @shoulders:      arm swing joint names; nullptr entries skip that arm
 * @leg_joints:     joints per leg chain, tip included
 * @spine_first:    first spine joint, for lean and idle breathing
 * @spine_nr:       spine joint count; 0 disables lean and breathing
 *
 * gait_config_init() writes out the walking reference tuning,
 * gait_config_run_init() the running one; start from them and override
 * per character. Skeleton binding fields
 * (knee_dir, leg_joints, shoulders, spine range) are only read from
 * the humanoid's walk configuration.
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
    float           lean;
    float           speed;
    vec3            knee_dir;
    const char      *shoulders[2];
    unsigned int    leg_joints;
    unsigned int    spine_first;
    unsigned int    spine_nr;
} gait_config;

/**
 * gait_config_init() - fill a gait configuration with the walk defaults
 * @cfg:    the configuration; fully overwritten
 */
void gait_config_init(gait_config *cfg);

/**
 * gait_config_run_init() - fill a gait configuration with the run defaults
 * @cfg:    the configuration; fully overwritten
 *
 * Faster cycle, longer stride, higher steps, a swing fraction above
 * one half (flight phases), deeper crouch, stronger bob and arm swing,
 * forward lean.
 */
void gait_config_run_init(gait_config *cfg);

/**
 * struct idle_config - what the biped does when it stops
 * @enter_speed:    drop below this to idle
 * @exit_speed:     rise above this to move again
 * @settle_rate:    how fast animation layers ease between idle and
 *                  locomotion, 1/s
 * @bob:            breathing bob amplitude
 * @bob_frequency:  rad/s
 * @sway_amplitude: breathing sway over the spine range, radians; needs
 *                  gait_config::spine_nr
 *
 * Idling holds the feet where they last landed (a swing in flight
 * still completes), eases the arm swing and bob out, and breathes:
 * a slow bob plus a subtle spine sway.
 */
typedef struct idle_config {
    float           enter_speed;
    float           exit_speed;
    float           settle_rate;
    float           bob;
    float           bob_frequency;
    float           sway_amplitude;
} idle_config;

/**
 * idle_config_init() - fill an idle configuration with the defaults
 * @cfg:    the configuration; fully overwritten
 */
void idle_config_init(idle_config *cfg);

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
 * struct humanoid - a biped walking, running and idling on IK legs
 * @scene:      the scene, for the animation clock (required)
 * @e:          the entity to drive (required); the humanoid does not
 *              own it and claims its update callback and priv pointer
 * @steer:      body driver; nullptr stands in place
 * @steer_priv: the steer callback's own state
 * @walk:       the walk tuning; zero cycle picks the defaults
 * @run:        the run tuning; zero cycle picks the defaults
 * @idle:       what to do when stopped; zero enter_speed picks the
 *              defaults
 *
 * The humanoid measures its own speed and picks the animation from
 * it: below idle_config::enter_speed it settles into breathing,
 * between the walk's and the run's authored speeds it blends the two
 * tunings continuously. The rest is internal: leg IK chains, gait
 * state, blend levels. humanoid_gait_start() resolves the legs
 * through the semantic JOINT_FOOT_LEFT/JOINT_FOOT_RIGHT joint types,
 * the arms and spine by name/range from the walk configuration,
 * installs the update callback; humanoid_gait_stop() restores
 * everything.
 */
struct humanoid {
    struct scene    *scene;
    entity3d        *e;
    humanoid_steer  steer;
    void            *steer_priv;
    gait_config     walk;
    gait_config     run;
    idle_config     idle;

    ik_chain        legs[2];
    gait_leg        feet[2];
    gait            gait;
    int             shoulders[2];
    int             (*orig_update)(entity3d *, void *);
    vec3            prev_pos;
    float           speed;
    float           blend;      /* 0: walk, 1: run */
    float           activity;   /* 0: idle, 1: moving */
    bool            idling;
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
