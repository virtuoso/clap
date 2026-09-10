/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SKELETON_H__
#define __CLAP_SKELETON_H__

#include "error.h"
#include "linmath.h"
#include "mesh.h"
#include "model.h"

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

#endif /* __CLAP_SKELETON_H__ */
