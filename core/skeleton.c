// SPDX-License-Identifier: Apache-2.0
/*
 * Procedural skeletons: a table of named joints with bind positions
 * turns into a rigidly skinned cube-per-joint mesh (skeleton_emit())
 * and into model3d skinning data (skeleton_apply()), giving procedural
 * characters the same joint pipeline that gltf models use.
 */
#include <limits.h>
#include <string.h>
#include "common.h"
#include "memory.h"
#include "primitives.h"
#include "shader_constants.h"
#include "skeleton.h"

/* joint indices live in unsigned char vertex attributes */
static_assert(JOINTS_MAX <= UCHAR_MAX + 1, "JOINTS_MAX doesn't fit vertex attributes");

/* a cube is 6 faces, 2 triangles each, no vertex sharing: hard edges */
#define CUBE_VERTICES (6 * 2 * 3)

static void cube_emit(const vec3 org, float half, const prim_emit_opts *po)
{
    /* faces are wound so that cross(q3 - q0, q1 - q0) points outward */
    static const float faces[6][4][3] = {
        { { -1,  1, -1 }, {  1,  1, -1 }, {  1,  1,  1 }, { -1,  1,  1 } },
        { { -1, -1,  1 }, {  1, -1,  1 }, {  1, -1, -1 }, { -1, -1, -1 } },
        { {  1, -1, -1 }, {  1, -1,  1 }, {  1,  1,  1 }, {  1,  1, -1 } },
        { { -1, -1, -1 }, { -1,  1, -1 }, { -1,  1,  1 }, { -1, -1,  1 } },
        { { -1, -1,  1 }, { -1,  1,  1 }, {  1,  1,  1 }, {  1, -1,  1 } },
        { {  1, -1, -1 }, {  1,  1, -1 }, { -1,  1, -1 }, { -1, -1, -1 } },
    };

    for (int f = 0; f < 6; f++) {
        vec3 quad[4];

        for (int v = 0; v < 4; v++) {
            quad[v][0] = org[0] + faces[f][v][0] * half;
            quad[v][1] = org[1] + faces[f][v][1] * half;
            quad[v][2] = org[2] + faces[f][v][2] * half;
        }

        _prim_emit_quad(quad, po);
    }
}

cerr _skeleton_emit(const skeleton_emit_opts *opts)
{
    const skel_joint *joints = opts->joints;
    size_t nr = opts->nr_joints;

    if (!opts->mesh || !joints || !nr || nr > JOINTS_MAX)   return CERR_INVALID_ARGUMENTS;

    /* the emitter owns the whole mesh: skin attributes cover every vertex */
    if (mesh_nr_vx(opts->mesh)) return CERR_INVALID_ARGUMENTS;

    size_t nr_cubes = 0;
    for (size_t i = 0; i < nr; i++)
        if (joints[i].size > 0.0f)  nr_cubes++;

    size_t nr_vx = nr_cubes * CUBE_VERTICES;
    if (!nr_vx)                     return CERR_INVALID_ARGUMENTS;
    if (nr_vx > USHRT_MAX + 1ul)    return CERR_TOO_LARGE;

    prim_emit_opts po = { .mesh = opts->mesh };
    CRES_RET_CERR(_prim_begin(nr_vx, &po));

    LOCAL_SET(uchar, vx_joints) = mem_alloc(sizeof(unsigned char) * 4, .nr = nr_vx);
    LOCAL_SET(float, vx_weights) = mem_alloc(sizeof(float) * 4, .nr = nr_vx, .zero = 1);
    if (!vx_joints || !vx_weights)  return CERR_NOMEM;

    size_t vx = 0;
    for (size_t i = 0; i < nr; i++) {
        if (joints[i].size <= 0.0f) continue;

        cube_emit(joints[i].pos, joints[i].size / 2.0f, &po);

        for (size_t v = vx; v < vx + CUBE_VERTICES; v++) {
            vx_joints[v * 4 + 0] = i;
            vx_joints[v * 4 + 1] = 0;
            vx_joints[v * 4 + 2] = 0;
            vx_joints[v * 4 + 3] = 0;
            vx_weights[v * 4 + 0] = 1.0f;
        }
        vx += CUBE_VERTICES;
    }

    CERR_RET_CERR(mesh_attr_dup(opts->mesh, MESH_JOINTS, vx_joints, sizeof(unsigned char) * 4, nr_vx));

    return mesh_attr_dup(opts->mesh, MESH_WEIGHTS, vx_weights, sizeof(float) * 4, nr_vx);
}

cerr skeleton_apply(model3d *m, const skel_joint *joints, size_t nr_joints)
{
    if (!m || !joints || !nr_joints || nr_joints > JOINTS_MAX || m->nr_joints)  return CERR_INVALID_ARGUMENTS;

    /* exactly one root, valid parents, no cycles, names present */
    size_t nr_roots = 0;
    for (size_t i = 0; i < nr_joints; i++) {
        if (!joints[i].name)                    return CERR_INVALID_ARGUMENTS;
        if (joints[i].parent < 0) {
            nr_roots++;
            continue;
        }
        if (joints[i].parent >= (int)nr_joints || joints[i].parent == (int)i)   return CERR_INVALID_ARGUMENTS;
    }
    if (nr_roots != 1)  return CERR_INVALID_ARGUMENTS;

    for (size_t i = 0; i < nr_joints; i++) {
        int hops, j = (int)i;

        for (hops = 0; j >= 0 && hops <= (int)nr_joints; hops++)    j = joints[j].parent;
        if (hops > (int)nr_joints)                                  return CERR_INVALID_ARGUMENTS;
    }

    mat4x4 *invmxs = mem_alloc(sizeof(mat4x4), .nr = nr_joints);
    if (!invmxs)    return CERR_NOMEM;

    for (size_t i = 0; i < nr_joints; i++)
        mat4x4_translate(invmxs[i], -joints[i].pos[0], -joints[i].pos[1], -joints[i].pos[2]);

    int err = model3d_add_skinning(m, nr_joints, invmxs);
    mem_free(invmxs);
    if (err)    return CERR_NOMEM;

    mat4x4_identity(m->root_pose);

    for (size_t i = 0; i < nr_joints; i++) {
        m->joints[i].name = strdup(joints[i].name);
        m->joints[i].id = i;

        if (joints[i].parent < 0) {
            m->root_joint = i;
        } else {
            int *child = darray_add(m->joints[joints[i].parent].children);

            if (!child) return CERR_NOMEM;
            *child = i;
            m->joints[i].parent = joints[i].parent;
        }

        cres(joint_type) jt = joint_by_type_name(joints[i].name);
        if (!IS_CERR(jt))   m->joint_types[jt.val] = i;
    }

    return CERR_OK;
}
