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

/* default far-end margin for skeleton_fit_chain() */
#define FIT_MARGIN 0.12f

cerr _skeleton_fit_chain(const skeleton_fit_opts *opts)
{
    skel_joint *joints = opts->joints;
    size_t nr = opts->nr_joints;

    if (!opts->mesh || !joints || nr < 2 || nr > JOINTS_MAX)    return CERR_INVALID_ARGUMENTS;

    size_t nr_vx = mesh_nr_vx(opts->mesh);
    if (!nr_vx) return CERR_INVALID_ARGUMENTS;

    vec3 axis;
    vec3_dup(axis, opts->axis);
    if (vec3_len(axis) == 0.0f) vec3_dup(axis, (vec3){ 0.0f, 1.0f, 0.0f });
    vec3_norm(axis, axis);

    float s_min = HUGE_VALF, s_max = -HUGE_VALF;
    for (size_t v = 0; v < nr_vx; v++) {
        const float *p = (const void *)mesh_vx(opts->mesh) + v * mesh_vx_stride(opts->mesh);
        float s = vec3_mul_inner(p, axis);

        s_min = fminf(s_min, s);
        s_max = fmaxf(s_max, s);
    }

    float margin = opts->margin ? : FIT_MARGIN;
    float span = (s_max - s_min) * (1.0f - margin);
    float band = span / (nr - 1);

    vec3 prev_perp = {};
    for (size_t k = 0; k < nr; k++) {
        float station = s_min + span * k / (nr - 1);
        vec3 acc = {};
        size_t cnt = 0;

        for (size_t v = 0; v < nr_vx; v++) {
            const float *p = (const void *)mesh_vx(opts->mesh) + v * mesh_vx_stride(opts->mesh);

            if (fabsf(vec3_mul_inner(p, axis) - station) > band)    continue;
            vec3_add(acc, acc, p);
            cnt++;
        }

        vec3 perp;
        if (cnt) {
            vec3_scale(acc, acc, 1.0f / cnt);
            vec3_add_scaled(perp, acc, axis, 1.0f, -vec3_mul_inner(acc, axis));
            vec3_dup(prev_perp, perp);
        } else {
            /* an empty band keeps the previous joint's lateral offset */
            vec3_dup(perp, prev_perp);
        }

        vec3_add_scaled(joints[k].pos, perp, axis, 1.0f, station);
    }

    return CERR_OK;
}

/* squared distance from @p to the (@a, @b) segment */
static float point_segment_dist2(const float *p, const vec3 a, const vec3 b)
{
    vec3 ab, ap, q;

    vec3_sub(ab, b, a);
    vec3_sub(ap, p, a);

    float len2 = vec3_mul_inner(ab, ab);
    float t = len2 > 0.0f ? clampf(vec3_mul_inner(ap, ab) / len2, 0.0f, 1.0f) : 0.0f;

    vec3_add_scaled(q, a, ab, 1.0f, t);
    vec3_sub(q, p, q);

    return vec3_mul_inner(q, q);
}

/* the bones of a joint: segments to each child; leaves extend their parent's */
/**
 * struct skin_bone - a segment that vertices get skinned to by distance
 * @joint:  the joint the segment belongs to
 * @a:      one end
 * @b:      the other end
 */
typedef struct skin_bone {
    size_t  joint;
    vec3    a, b;
} skin_bone;

static DEFINE_CLEANUP(skin_bone, mem_free(*p))

cerr _skeleton_skin(const skeleton_skin_opts *opts)
{
    const skel_joint *joints = opts->joints;
    size_t nr = opts->nr_joints;

    if (!opts->mesh || !joints || !nr || nr > JOINTS_MAX)   return CERR_INVALID_ARGUMENTS;

    size_t nr_vx = mesh_nr_vx(opts->mesh);
    if (!nr_vx || mesh_nr_joints(opts->mesh) || mesh_nr_weights(opts->mesh))    return CERR_INVALID_ARGUMENTS;

    LOCAL_SET(skin_bone, bones) = mem_alloc(sizeof(*bones), .nr = 2 * nr);
    LOCAL_SET(uchar, vx_joints) = mem_alloc(sizeof(unsigned char) * 4, .nr = nr_vx);
    LOCAL_SET(float, vx_weights) = mem_alloc(sizeof(float) * 4, .nr = nr_vx, .zero = 1);

    if (!bones || !vx_joints || !vx_weights)    return CERR_NOMEM;

    size_t nr_bones = 0;
    float seg_len = 0.0f;
    for (size_t i = 0; i < nr; i++) {
        bool leaf = true;

        for (size_t c = 0; c < nr; c++) {
            if (joints[c].parent != (int)i) continue;

            vec3 seg;

            vec3_sub(seg, joints[c].pos, joints[i].pos);
            seg_len += vec3_len(seg);

            leaf = false;
            bones[nr_bones].joint = i;
            vec3_dup(bones[nr_bones].a, joints[i].pos);
            vec3_dup(bones[nr_bones].b, joints[c].pos);
            nr_bones++;
        }

        if (!leaf)  continue;

        /* a leaf covers the region past itself: extend its parent's segment */
        bones[nr_bones].joint = i;
        vec3_dup(bones[nr_bones].a, joints[i].pos);
        vec3_dup(bones[nr_bones].b, joints[i].pos);
        if (joints[i].parent >= 0) {
            vec3 dir;

            vec3_sub(dir, joints[i].pos, joints[joints[i].parent].pos);
            vec3_add(bones[nr_bones].b, bones[nr_bones].b, dir);
        }
        nr_bones++;
    }

    float eps2 = seg_len > 0.0f ? powf(seg_len / nr_bones * 0.01f, 2.0f) : 1e-6f;

    for (size_t v = 0; v < nr_vx; v++) {
        const float *pos = (void *)mesh_vx(opts->mesh) + v * mesh_vx_stride(opts->mesh);
        float best_d2[4] = { HUGE_VALF, HUGE_VALF, HUGE_VALF, HUGE_VALF };
        size_t best_j[4] = {};

        for (size_t b = 0; b < nr_bones; b++) {
            float d2 = point_segment_dist2(pos, bones[b].a, bones[b].b);

            /* the same joint may already be in with a closer bone */
            size_t slot;
            for (slot = 0; slot < 4; slot++)
                if (best_d2[slot] < HUGE_VALF && best_j[slot] == bones[b].joint)    break;
            if (slot < 4) {
                if (d2 >= best_d2[slot])    continue;

                /* the slots stay sorted: move the closer one forward */
                best_d2[slot] = d2;
                for (; slot && best_d2[slot] < best_d2[slot - 1]; slot--) {
                    float d = best_d2[slot - 1];
                    size_t j = best_j[slot - 1];

                    best_d2[slot - 1] = best_d2[slot];
                    best_j[slot - 1] = best_j[slot];
                    best_d2[slot] = d;
                    best_j[slot] = j;
                }
                continue;
            }

            for (slot = 0; slot < 4; slot++)
                if (d2 < best_d2[slot]) break;
            if (slot == 4)              continue;

            for (size_t s = 3; s > slot; s--) {
                best_d2[s] = best_d2[s - 1];
                best_j[s] = best_j[s - 1];
            }
            best_d2[slot] = d2;
            best_j[slot] = bones[b].joint;
        }

        float total = 0.0f;
        float w[4] = {};
        for (int s = 0; s < 4; s++) {
            if (best_d2[s] == HUGE_VALF)    break;
            w[s] = 1.0f / (best_d2[s] + eps2);
            total += w[s];
        }

        for (int s = 0; s < 4; s++) {
            vx_joints[v * 4 + s] = best_d2[s] < HUGE_VALF ? best_j[s] : 0;
            vx_weights[v * 4 + s] = total > 0.0f ? w[s] / total : !s;
        }
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

cresp(model3d) skeleton_model_new(pipeline *pl, struct mesh *mesh, const skel_joint *joints,
                                  size_t nr_joints, const char *name)
{
    /*
     * The model3d constructor consumes the shader ref (ref_pass), so a plain
     * prog needs no cleanup between here and the handoff
     */
    struct shader_prog *prog = CRES_RET_T(pipeline_shader_find_get(pl, "model"), model3d);
    LOCAL_SET(model3d, m) = CRES_RET_T(
        ref_new_checked(model3d, .prog = ref_pass(prog), .mesh = mesh, .name = name),
        model3d);
    CERR_RET_T(skeleton_apply(m, joints, nr_joints), model3d);

    return cresp_val(model3d, NOCU(m));
}

cresp(model3d) _skeleton_rig_model(const skeleton_rig_opts *opts)
{
    if (!opts->pipeline || !opts->mesh || !opts->joints || opts->nr_joints < 2)
        return cresp_error(model3d, CERR_INVALID_ARGUMENTS);

    /* fit the chain to the mesh, then bind the vertices to it */
    skeleton_fit_opts fit = {
        .mesh       = opts->mesh,
        .joints     = opts->joints,
        .nr_joints  = opts->nr_joints,
        .margin     = opts->margin,
    };
    vec3_dup(fit.axis, opts->axis);
    CERR_RET_T(_skeleton_fit_chain(&fit), model3d);
    CERR_RET_T(
        skeleton_skin(.mesh = opts->mesh, .joints = opts->joints, .nr_joints = opts->nr_joints),
        model3d
    );
    mesh_aabb_calc(opts->mesh);

    return skeleton_model_new(opts->pipeline, opts->mesh, opts->joints, opts->nr_joints, opts->name);
}
