/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_HUMANOID_H__
#define __CLAP_PROCANIM_HUMANOID_H__

#include "error.h"
#include "linmath.h"
#include "mesh.h"
#include "model.h"
#include "ik.h"
#include "skeleton.h"
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

/* joint indices into humanoid::skel; the pelvis is the root */
enum humanoid_joint {
    HUMANOID_PELVIS = 0,
    HUMANOID_SPINE,
    HUMANOID_CHEST,
    HUMANOID_HEAD,
    HUMANOID_SHOULDER_L,
    HUMANOID_ARM_UPPER_L,
    HUMANOID_ARM_LOWER_L,
    HUMANOID_HAND_L,
    HUMANOID_SHOULDER_R,
    HUMANOID_ARM_UPPER_R,
    HUMANOID_ARM_LOWER_R,
    HUMANOID_HAND_R,
    HUMANOID_HIP_L,
    HUMANOID_SHIN_L,
    HUMANOID_FOOT_L,
    HUMANOID_HIP_R,
    HUMANOID_SHIN_R,
    HUMANOID_FOOT_R,
    HUMANOID_NR_JOINTS
};

/**
 * struct humanoid_config - a biped's body plan
 * @pelvis_y:       pelvis (the root joint) height
 * @spine_y:        spine joint height
 * @chest_y:        chest joint height; the arms hang off the chest
 * @head_y:         head joint height
 * @shoulder_x:     shoulder lateral offset from the center line...
 * @shoulder_y:     ...and height
 * @arm_x:          lateral offset of the hanging arm column: the upper
 *                  arm, the forearm and the hand
 * @arm_upper_y:    upper arm joint height
 * @arm_lower_y:    forearm joint height
 * @hand_y:         hand joint height
 * @hip_x:          lateral hip offset from the center line; the leg
 *                  column hangs straight down from it, and
 *                  gait_config::hip_x wants to match so the feet track
 *                  under the hips
 * @hip_y:          hip joint height
 * @shin_y:         shin joint height
 * @foot_y:         foot joint height; the bind pose stands with the
 *                  feet planted, so gait_config::foot_y wants to match
 * @size:           cube edge per joint (skel_joint::size), indexed by
 *                  &enum humanoid_joint, for humanoid_mesh_cubes(); a
 *                  thickness hint for other generators
 *
 * All positions are model space with the origin on the ground between
 * the feet. humanoid_config_init() writes out the reference body (the
 * reference body); start from it and override per character.
 */
typedef struct humanoid_config {
    float           pelvis_y;
    float           spine_y;
    float           chest_y;
    float           head_y;
    float           shoulder_x;
    float           shoulder_y;
    float           arm_x;
    float           arm_upper_y;
    float           arm_lower_y;
    float           hand_y;
    float           hip_x;
    float           hip_y;
    float           shin_y;
    float           foot_y;
    float           size[HUMANOID_NR_JOINTS];
} humanoid_config;

/**
 * humanoid_config_init() - fill a body plan with the defaults
 * @cfg:    the configuration; fully overwritten
 */
void humanoid_config_init(humanoid_config *cfg);

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
 * entity's Y with the ground height under it plus gait_config::base_y
 * plus bob. Speed is measured from the entity's horizontal motion, so
 * steering doesn't report it.
 */
typedef void (*humanoid_steer)(humanoid *h, double t, float dt);

/**
 * typedef humanoid_mesh_fn - body mesh generator callback
 * @m:  the mesh to emit into
 * @h:  the humanoid; humanoid::cfg and the skeleton (humanoid::skel)
 *      are final when the generator runs
 *
 * Called by humanoid_model() between building the skeleton and skinning
 * the mesh: emit any geometry over the bind-pose joints
 * (core/primitives.h). A generator that leaves the mesh unskinned gets
 * the automatic nearest-bone binding (skeleton_skin()); one that writes
 * the skinning attributes itself (skeleton_emit() does) is left alone.
 * A generator that wants part of its auto-skinned geometry rigid (a
 * machine's hull) emits that part first and sets humanoid::rigid_nr.
 * humanoid::mesh_priv carries the generator's own parameters.
 *
 * Return: CERR_OK or an emit error
 */
typedef cerr (*humanoid_mesh_fn)(mesh_t *m, humanoid *h);

/**
 * humanoid_mesh_cubes() - the cube construct mesh
 * @m:  the mesh to emit into
 * @h:  the humanoid
 *
 * A rigid cube per joint (skeleton_emit()), edges from
 * humanoid_config::size: the reference body. The default when
 * humanoid::mesh is nullptr.
 */
cerr humanoid_mesh_cubes(mesh_t *m, humanoid *h);

/**
 * struct humanoid - a biped walking, running and idling on IK legs
 * @scene:      the scene, for the animation clock (required)
 * @e:          the entity to drive (required); the humanoid does not
 *              own it and claims its update callback and priv pointer
 * @name:       model and mesh name for humanoid_model(); nullptr picks
 *              "humanoid"
 * @steer:      body driver; nullptr stands in place
 * @steer_priv: the steer callback's own state
 * @ground:     ground height probe for the body and the feet; nullptr:
 *              flat ground at 0
 * @ground_priv: the probe's own state (a navmesh, a terrain)
 * @mesh:       body mesh generator for humanoid_model(); nullptr picks
 *              humanoid_mesh_cubes()
 * @mesh_priv:  the generator's own state
 * @rigid_nr:   set by an auto-skinned mesh generator: this many leading
 *              mesh vertices bind rigidly to the pelvis instead of the
 *              automatic nearest-bone skinning, which would let the
 *              swinging limbs drag them (a machine's hull must not
 *              wobble)
 * @cfg:        the body plan for humanoid_model(); a zero pelvis_y
 *              picks the defaults
 * @walk:       the walk tuning; zero cycle picks the defaults
 * @run:        the run tuning; zero cycle picks the defaults
 * @idle:       what to do when stopped; zero enter_speed picks the
 *              defaults
 *
 * The mesh generator is the only part that differs between the cube
 * construct and whatever the caller emits: the skeleton, the gait and
 * the leg IK all run off the same body plan. The humanoid measures its
 * own speed and picks the animation from it: below
 * idle_config::enter_speed it settles into breathing, between the
 * walk's and the run's authored speeds it blends the two tunings
 * continuously. The rest is internal: the joint table humanoid_model()
 * builds, leg IK chains, gait state, blend levels.
 * humanoid_gait_start() resolves the legs through the semantic
 * JOINT_FOOT_LEFT/JOINT_FOOT_RIGHT joint types, the arms and spine by
 * name/range from the walk configuration, installs the update
 * callback; humanoid_gait_stop() restores everything.
 */
struct humanoid {
    struct scene    *scene;
    entity3d        *e;
    const char      *name;
    humanoid_steer  steer;
    void            *steer_priv;
    gait_ground_fn  ground;
    void            *ground_priv;
    humanoid_mesh_fn mesh;
    void            *mesh_priv;
    size_t          rigid_nr;
    humanoid_config cfg;
    gait_config     walk;
    gait_config     run;
    idle_config     idle;

    skel_joint      skel[HUMANOID_NR_JOINTS];
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
 * humanoid_model() - build the rigged body model
 * @h:  the humanoid, with humanoid::cfg, humanoid::mesh and
 *      humanoid::name optionally filled
 * @pl: pipeline the "model" shader is sourced from
 *
 * Build the skeleton from the body plan (a zero pelvis_y picks the
 * humanoid_config_init() defaults) and run the mesh generator over it;
 * a mesh the generator left unskinned is bound to the bones
 * (skeleton_skin()). The caller wraps the model in a model3dtx (texture
 * and material are the variant's other half), creates the entity and
 * hands it to humanoid_gait_start().
 * Context: model3d setup time
 * Return: the rigged model3d (caller owns), or a generator, skinning or
 * build error
 */
cresp_check(model3d) humanoid_model(humanoid *h, pipeline *pl);

/**
 * humanoid_gait_start() - put a biped on its feet
 * @h:  the humanoid, with humanoid::scene, humanoid::e and optionally
 *      the tunings (humanoid::walk, humanoid::run, humanoid::idle) and
 *      humanoid::steer filled
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
