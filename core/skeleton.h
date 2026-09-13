/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SKELETON_H__
#define __CLAP_SKELETON_H__

#include "error.h"
#include "linmath.h"
#include "mesh.h"
#include "model.h"
#include "pipeline.h"

/**
 * struct skel_joint - one joint of a procedural skeleton
 * @name:   joint name, unique within the skeleton (required); names
 *          matching semantic joint types ("head", "hand_left", ...) map
 *          the joint to the corresponding &enum joint_type
 * @parent: index of the parent joint; -1 for the root, of which there
 *          must be exactly one
 * @pos:    bind position, model space; bind rotations are identity
 * @size:   cube edge for skeleton_emit(); 0 emits no cube
 */
typedef struct skel_joint {
    const char  *name;
    int         parent;
    vec3        pos;
    float       size;
} skel_joint;

/**
 * struct skeleton_emit_opts - skeleton mesh emitter options
 * @mesh:       empty mesh to emit into (required)
 * @joints:     the skeleton (required)
 * @nr_joints:  number of joints
 *
 * skeleton_emit() emits an axis-aligned cube per joint, centered on the
 * joint's bind position and rigidly skinned to it. The mesh must be
 * empty: the emitter owns the whole vertex range so that the skinning
 * attributes it writes cover every vertex.
 */
typedef struct skeleton_emit_opts {
    struct mesh         *mesh;
    const skel_joint    *joints;
    size_t              nr_joints;
} skeleton_emit_opts;

cerr_check _skeleton_emit(const skeleton_emit_opts *opts);

/* Emit a cube per skeleton joint, rigidly skinned to it */
#define skeleton_emit(args...) _skeleton_emit(&(skeleton_emit_opts){ args })

/**
 * struct skeleton_skin_opts - automatic mesh skinning options
 * @mesh:       mesh with vertices and without skinning attributes
 *              (required)
 * @joints:     the skeleton (required)
 * @nr_joints:  number of joints
 *
 * skeleton_skin() binds an existing mesh to a skeleton by distance:
 * every vertex is weighted to the joints whose bones (the segments from
 * a joint to each of its children; leaf joints extend their parent
 * segment onward) pass closest to it, up to 4 influences with inverse
 * square falloff. Meant for meshes authored without an armature (trees,
 * props) that a procedural skeleton is fitted to after the fact; cube
 * sizes in @joints are ignored.
 */
typedef struct skeleton_skin_opts {
    struct mesh         *mesh;
    const skel_joint    *joints;
    size_t              nr_joints;
} skeleton_skin_opts;

cerr_check _skeleton_skin(const skeleton_skin_opts *opts);

/* Skin an existing mesh to a skeleton by bone distance */
#define skeleton_skin(args...) _skeleton_skin(&(skeleton_skin_opts){ args })

/**
 * struct skeleton_fit_opts - chain fitting options
 * @mesh:       mesh to fit the chain to (required)
 * @joints:     skeleton whose positions get filled in; names, parent
 *              links and sizes are the caller's (required)
 * @nr_joints:  number of joints, at least 2
 * @axis:       axis to slice the mesh along; zero picks +Y
 * @margin:     fraction of the extent to stop short of the far end so
 *              the last joint keeps a lever arm; 0 picks the default
 *
 * skeleton_fit_chain() places a joint chain inside an unrigged mesh:
 * joints at even stations along @axis, each at the centroid of the
 * vertices in a band around its station, so the chain follows the
 * mesh's lean and mass distribution (a tree trunk, a lamppost, a
 * seaweed strand). Pair with skeleton_skin() to rig the mesh to the
 * fitted chain.
 */
typedef struct skeleton_fit_opts {
    struct mesh         *mesh;
    skel_joint          *joints;
    size_t              nr_joints;
    vec3                axis;
    float               margin;
} skeleton_fit_opts;

cerr_check _skeleton_fit_chain(const skeleton_fit_opts *opts);

/* Fit a joint chain to a mesh along an axis */
#define skeleton_fit_chain(args...) _skeleton_fit_chain(&(skeleton_fit_opts){ args })

/**
 * skeleton_apply() - add a procedural skeleton to a model
 * @m:          model3d built from a skeleton_emit() mesh
 * @joints:     the same skeleton the mesh was emitted from
 * @nr_joints:  number of joints
 *
 * Set up the model's skinning from the skeleton: inverse bind matrices
 * from the joints' bind positions, joint names, the parent-child tree
 * and the semantic joint type mapping. The model renders at bind pose
 * until animation or a procedural pose (entity3d_pose_*(), ik_chain_*())
 * drives the joints.
 * Context: model3d setup time
 * Return: CERR_OK, CERR_NOMEM or CERR_INVALID_ARGUMENTS (no or multiple
 * roots, dangling or cyclic parents, missing names, too many joints).
 */
cerr_check skeleton_apply(model3d *m, const skel_joint *joints, size_t nr_joints);

/**
 * skeleton_model_new() - build a model that carries a skeleton
 * @pl:         pipeline the "model" shader is sourced from
 * @mesh:       the mesh, skinned to @joints; borrowed
 * @joints:     the skeleton
 * @nr_joints:  number of joints
 * @name:       model name
 *
 * Return: the model3d with its skinning set up by skeleton_apply()
 *         (caller owns), or an error
 */
cresp_check(model3d) skeleton_model_new(pipeline *pl, struct mesh *mesh, const skel_joint *joints,
                                        size_t nr_joints, const char *name);

/**
 * struct skeleton_rig_opts - fit, skin and build a rigged model in one step
 * @pipeline:   pipeline the "model" shader is sourced from (required)
 * @mesh:       unrigged mesh to rig; borrowed, its skinning attributes get
 *              filled in (required)
 * @joints:     the joint chain; positions are filled by the fit, the names,
 *              parent links and sizes are the caller's (required)
 * @nr_joints:  number of joints, at least 2
 * @name:       model name
 * @axis:       fit axis, passed to skeleton_fit_chain(); zero picks +Y
 * @margin:     fit margin, passed to skeleton_fit_chain(); 0 picks the default
 *
 * The whole mesh side of procedural rigging in one call: skeleton_fit_chain()
 * to place the chain along the mesh, skeleton_skin() to bind the vertices, and
 * a model3d carrying the skinning (skeleton_apply()), for any caller turning
 * an unrigged mesh into a rigged model. The mesh is left rigged and stays
 * the caller's.
 * Return: the rigged model3d (caller owns), or a fit/skin/build error.
 */
typedef struct skeleton_rig_opts {
    pipeline    *pipeline;
    struct mesh *mesh;
    skel_joint  *joints;
    size_t      nr_joints;
    const char  *name;
    vec3        axis;
    float       margin;
} skeleton_rig_opts;

cresp_check(model3d) _skeleton_rig_model(const skeleton_rig_opts *opts);

/* Fit a chain to a mesh, skin it, and build the rigged model3d */
#define skeleton_rig_model(args...) _skeleton_rig_model(&(skeleton_rig_opts){ args })

#endif /* __CLAP_SKELETON_H__ */
