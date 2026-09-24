/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_ARACHNID_H__
#define __CLAP_PROCANIM_ARACHNID_H__

#include "error.h"
#include "linmath.h"
#include "mesh.h"
#include "model.h"
#include "ik.h"
#include "skeleton.h"
#include "animator.h"
#include "procanim/gait.h"

struct scene;

#define ARACHNID_NR_LEGS    8
#define ARACHNID_LEG_JOINTS 3   /* hip, knee, foot */
#define ARACHNID_NR_JOINTS  (2 + ARACHNID_NR_LEGS * ARACHNID_LEG_JOINTS)
/** ARACHNID_MAX_EXTRA - the most joints a caller adds after the body's */
#define ARACHNID_MAX_EXTRA  4

/**
 * struct arachnid_config - an eight-legged walker, fully parameterized
 * @ceph_y:         cephalothorax center height...
 * @ceph_z:         ...and fore/aft position (forward, raised above the
 *                  leg plane)
 * @ceph_r:         cephalothorax radius
 * @abd_y:          abdomen center height...
 * @abd_z:          ...and fore/aft position (back, overlapping the
 *                  cephalothorax into a connected two-lobe body)
 * @abd_r:          abdomen radius
 * @hip_y:          leg-attach height, on the body's lower flank
 * @hip_r:          leg root radius (lateral) around the body
 * @fan_z:          fore/aft center of the leg fan; the knees and feet
 *                  radiate from it
 * @root_spread:    fore/aft spread of the leg roots; more than @hip_r
 *                  puts the front roots on the cephalothorax and the
 *                  rear ones back on the abdomen flank, so no leg
 *                  crosses the body
 * @knee_y:         knee height, tented above the body
 * @knee_r:         knee reach radius
 * @foot_y:         planted foot height above the ground
 * @foot_r:         foot rest radius
 * @leg_r:          leg thickness at the hip, the knee and the tip, for
 *                  the mesh generators
 * @fan:            leg fan angles from +X toward +Z, front pair to back,
 *                  mirrored on the left side: 90 points straight
 *                  forward, 0 straight out to the side, negative back
 * @cycle:          seconds per gait cycle
 * @swing:          fraction of the cycle each foot is airborne; below
 *                  0.5 a tetrapod always has planted feet
 * @stride:         distance between successive footfalls of one leg
 * @step_height:    swing arc height
 *
 * All positions are model space with the origin on the ground between
 * the feet. arachnid_config_init() writes out the reference body (the
 * ldjam56 garden spider); start from it and override per creature.
 */
typedef struct arachnid_config {
    float           ceph_y;
    float           ceph_z;
    float           ceph_r;
    float           abd_y;
    float           abd_z;
    float           abd_r;
    float           hip_y;
    float           hip_r;
    float           fan_z;
    float           root_spread;
    float           knee_y;
    float           knee_r;
    float           foot_y;
    float           foot_r;
    float           leg_r[ARACHNID_LEG_JOINTS];
    float           fan[ARACHNID_NR_LEGS / 2];
    float           cycle;
    float           swing;
    float           stride;
    float           step_height;
} arachnid_config;

/**
 * arachnid_config_init() - fill a configuration with the defaults
 * @cfg:    the configuration; fully overwritten
 */
void arachnid_config_init(arachnid_config *cfg);

typedef struct arachnid arachnid;

/**
 * typedef arachnid_steer - body driver callback
 * @a:  the arachnid
 * @t:  animation time, seconds, anchored at gait start
 * @dt: seconds since the previous update
 *
 * Called at the top of every update: drive the entity's position and
 * heading (entity3d_position(), entity3d_rotate()) however the creature
 * moves. The arachnid owns the vertical: it overwrites the entity's Y
 * with the ground height under it. Speed is measured from the entity's
 * horizontal motion, so steering doesn't report it.
 */
typedef void (*arachnid_steer)(arachnid *a, double t, float dt);

/**
 * typedef arachnid_mesh_fn - body mesh generator callback
 * @m:  the mesh to emit into
 * @a:  the arachnid; arachnid::cfg and the skeleton (arachnid::skel)
 *      are final when the generator runs
 *
 * Called by arachnid_model() between building the skeleton and skinning
 * the mesh: emit any geometry over the bind-pose joints
 * (core/primitives.h); the whole mesh is then bound to the nearest
 * bones automatically (skeleton_skin()). A generator that wants part of
 * its geometry rigid (a machine's hull) emits that part first and sets
 * arachnid::rigid_nr. arachnid::mesh_priv carries the generator's own
 * parameters.
 *
 * Return: CERR_OK or an emit error
 */
typedef cerr (*arachnid_mesh_fn)(mesh_t *m, arachnid *a);

/**
 * arachnid_mesh_organic() - the fleshy spider mesh
 * @m:  the mesh to emit into
 * @a:  the arachnid
 *
 * Cephalothorax and abdomen spheres and tapered eight-sided legs. The
 * default when arachnid::mesh is nullptr.
 */
cerr arachnid_mesh_organic(mesh_t *m, arachnid *a);

/**
 * arachnid_mesh_tank() - the spider tank mesh
 * @m:  the mesh to emit into
 * @a:  the arachnid
 *
 * A flat eight-sided puck hull over the leg fan, its deck at the
 * cephalothorax joint (a turret pad for later), square-section strut
 * legs pivoting on fixed shoulder pods at the hull wall, ball joints
 * at the knees.
 */
cerr arachnid_mesh_tank(mesh_t *m, arachnid *a);

/**
 * struct arachnid - an eight-legged walker on IK legs
 * @scene:      the scene the body is in; the rig runs on the animator's
 *              clock and doesn't read it
 * @e:          the entity to drive (required by arachnid_gait_init());
 *              the arachnid does not own it
 * @name:       model and mesh name for arachnid_model(); nullptr picks
 *              "arachnid"
 * @steer:      body driver; nullptr stands in place
 * @steer_priv: the steer callback's own state
 * @ground:     ground height probe for the body and the feet; nullptr:
 *              flat ground at 0
 * @ground_priv: the probe's own state (a navmesh, a terrain)
 * @mesh:       body mesh generator for arachnid_model(); nullptr picks
 *              arachnid_mesh_organic()
 * @mesh_priv:  the generator's own state
 * @rigid_nr:   set by the mesh generator: this many leading mesh
 *              vertices bind rigidly to the root joint instead of the
 *              automatic nearest-bone skinning, which would let the
 *              swinging legs drag them (a machine's hull must not
 *              wobble)
 * @extra:      the caller's joints appended after the body's, up to
 *              ARACHNID_MAX_EXTRA (a turret on the deck): parent indices
 *              are the arachnid's, 0 for the body, ARACHNID_NR_JOINTS + i
 *              for the i-th extra; the names must outlive the model build
 * @nr_extra:   how many
 * @cfg:        the body plan and gait tuning; zero cycle picks the
 *              defaults
 *
 * The mesh generator is the only part that differs between an organic
 * spider, a spider tank and whatever the caller emits: the skeleton,
 * the gait and the leg IK all run off the same body plan. The rest is
 * internal: the joint table arachnid_model() builds, leg IK chains,
 * gait state. arachnid_gait_init() builds them; the rig then plays as a
 * pose class (arachnid_pose_ops) bound to the entity, with the arachnid
 * as the binding.
 */
struct arachnid {
    struct scene        *scene;
    entity3d            *e;
    const char          *name;
    arachnid_steer      steer;
    void                *steer_priv;
    gait_ground_fn      ground;
    void                *ground_priv;
    arachnid_mesh_fn    mesh;
    void                *mesh_priv;
    size_t              rigid_nr;
    const skel_joint    *extra;
    size_t              nr_extra;
    arachnid_config     cfg;
    float               speed;

    skel_joint          skel[ARACHNID_NR_JOINTS + ARACHNID_MAX_EXTRA];
    ik_chain            legs[ARACHNID_NR_LEGS];
    gait_leg            feet[ARACHNID_NR_LEGS];
    vec3                pole[ARACHNID_NR_LEGS];  /* per-leg knee-up hint, model space */
    gait                gait;
    vec3                prev_pos;
    char                names[ARACHNID_NR_JOINTS][16];
};

/**
 * arachnid_pose_ops - the walker as a pose class
 *
 * anim_lib_pose(lib, &arachnid_pose_ops, nullptr) on the model, then
 * animator_bind() with an arachnid_gait_init()ed arachnid on each entity.
 * eval steers if arachnid::steer is set, sits the body on the ground and
 * poses the legs from how fast it is moving; start re-anchors the speed
 * measurement.
 */
extern const anim_pose_ops arachnid_pose_ops;

/**
 * arachnid_model() - build the rigged body model
 * @a:  the arachnid, with arachnid::cfg, arachnid::mesh and
 *      arachnid::name optionally filled
 * @pl: pipeline the "model" shader is sourced from
 *
 * Build the skeleton from the configuration (a zero cycle picks the
 * arachnid_config_init() defaults) and the extra joints, run the mesh
 * generator over it and bind the mesh to the bones (skeleton_skin()).
 * The caller wraps the model in a model3dtx (texture and material are
 * the variant's other half), creates the entity and binds the rig
 * (arachnid_gait_init(), arachnid_pose_ops).
 * Context: model3d setup time
 * Return: the rigged model3d (caller owns), CERR_INVALID_ARGUMENTS for
 * too many extra joints, or a generator, skinning or build error
 */
cresp_check(model3d) arachnid_model(arachnid *a, pipeline *pl);

/**
 * arachnid_gait_init() - put the arachnid on its feet
 * @a:  the arachnid, with arachnid::e filled; the entity carries the
 *      arachnid_model() model of this same arachnid and stands where it
 *      spawns
 *
 * Builds the leg IK chains and the gait state and poses the bind pose.
 * Nothing moves until the rig plays: anim_lib_pose() it on the model,
 * animator_bind() it on the entity with @a, animator_play() it.
 * Context: entity setup time
 * Return: CERR_OK, CERR_NOMEM or CERR_INVALID_ARGUMENTS (no skeleton, bad
 * chain)
 */
cerr_check arachnid_gait_init(arachnid *a);

/**
 * arachnid_gait_done() - free what arachnid_gait_init() built
 * @a:  an initialized arachnid; the leg chains are freed
 */
void arachnid_gait_done(arachnid *a);

/**
 * arachnid_gait_start() - put the arachnid on its feet and set it walking
 * @a:  as for arachnid_gait_init()
 *
 * arachnid_gait_init(), then the pose class defined, bound and played on
 * the entity's base layer, for a rig that is the entity's only animation.
 * A convenience over the four calls.
 * Context: entity setup time
 * Return: as arachnid_gait_init(), or the animator's errors
 */
cerr_check arachnid_gait_start(arachnid *a);

/**
 * arachnid_gait_stop() - stop walking
 * @a:  a started arachnid; the base layer stops and the leg chains are
 *      freed
 */
void arachnid_gait_stop(arachnid *a);

#endif /* __CLAP_PROCANIM_ARACHNID_H__ */
