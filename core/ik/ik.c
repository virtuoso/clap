// SPDX-License-Identifier: Apache-2.0
/*
 * Inverse kinematics on entity3d skeletons: an ik_chain binds a run of
 * joints, FABRIK solves their positions in model space and the joints'
 * local rotations are rebuilt to track the solved segments.
 */
#include <math.h>
#include <string.h>
#include "common.h"
#include "memory.h"
#include "fabrik.h"
#include "ik.h"

/* segments closer than this to anchor-parallel fall back to the carried reference */
#define IK_POLE_EPS 1e-2f

/*
 * Roll reference for an anchored frame: @anchor orthogonalized against
 * @axis. When the two line up, fall back to @carry, the previous
 * segment's reference; @carry is updated on success.
 */
static bool frame_ref(vec3 ref, const vec3 axis, const vec3 anchor, vec3 carry)
{
    vec3 r;

    vec3_add_scaled(r, anchor, axis, 1.0f, -vec3_mul_inner(axis, anchor));
    if (vec3_len(r) < IK_POLE_EPS)      vec3_add_scaled(r, carry, axis, 1.0f, -vec3_mul_inner(axis, carry));
    if (vec3_len(r) < IK_POLE_EPS)      return false;

    vec3_norm(ref, r);
    vec3_dup(carry, ref);

    return true;
}

/*
 * Rotation taking the (@from_dir, @from_ref) orthonormal frame onto the
 * (@to_dir, @to_ref) one; directions and references must be unit length
 * and mutually perpendicular within each pair
 */
static void frame_rotation(quat q, const vec3 from_dir, const vec3 from_ref,
                           const vec3 to_dir, const vec3 to_ref)
{
    vec3 from_side, to_side;
    mat4x4 m;

    vec3_mul_cross(from_side, from_dir, from_ref);
    vec3_mul_cross(to_side, to_dir, to_ref);

    mat4x4_identity(m);
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++)
            m[col][row] = to_dir[row] * from_dir[col] +
                          to_ref[row] * from_ref[col] +
                          to_side[row] * from_side[col];

    quat_from_mat4x4(q, m);
}

/* minimal-arc rotation taking unit vector @a onto unit vector @b */
static void quat_from_vectors(quat q, const vec3 a, const vec3 b)
{
    float d = vec3_mul_inner(a, b);

    if (d < -1.0f + 1e-6f) {
        /* opposite vectors: half-turn around anything perpendicular to @a */
        vec3 axis;

        vec3_mul_cross(axis, a, (vec3){ 1.0f, 0.0f, 0.0f });
        if (vec3_mul_inner(axis, axis) < 1e-6f) vec3_mul_cross(axis, a, (vec3){ 0.0f, 1.0f, 0.0f });
        vec3_norm(axis, axis);

        q[0] = axis[0];
        q[1] = axis[1];
        q[2] = axis[2];
        q[3] = 0.0f;
        return;
    }

    vec3_mul_cross(q, a, b);
    q[3] = 1.0f + d;
    quat_norm(q, q);
}

cerr ik_chain_init(ik_chain *chain, entity3d *e, int tip, size_t nr)
{
    model3d *model = e->txmodel->model;

    *chain = (ik_chain){};

    if (!e->joints || nr < 2 || tip < 0 || tip >= (int)model->nr_joints)    return CERR_INVALID_ARGUMENTS;

    chain->e  = e;
    chain->nr = nr;
    chain->joints      = mem_alloc(sizeof(*chain->joints), .nr = nr);
    chain->points      = mem_alloc(sizeof(*chain->points), .nr = nr);
    chain->lengths     = mem_alloc(sizeof(*chain->lengths), .nr = nr - 1);
    chain->rest_points = mem_alloc(sizeof(*chain->rest_points), .nr = nr);
    chain->rest_dir    = mem_alloc(sizeof(*chain->rest_dir), .nr = nr - 1);
    chain->rest_rot    = mem_alloc(sizeof(*chain->rest_rot), .nr = nr);
    chain->rest_trans  = mem_alloc(sizeof(*chain->rest_trans), .nr = nr);
    chain->rest_scale  = mem_alloc(sizeof(*chain->rest_scale), .nr = nr);
    if (!chain->joints || !chain->points || !chain->lengths ||
        !chain->rest_points || !chain->rest_dir || !chain->rest_rot ||
        !chain->rest_trans || !chain->rest_scale)
        goto err_nomem;

    /* walk up the skeleton from the tip */
    int joint = tip;
    for (ssize_t k = nr - 1; k >= 0; k--) {
        if (joint < 0)  goto err_args;
        chain->joints[k] = joint;
        joint = model->joints[joint].parent;
    }

    /* @joint is now the chain root's parent or -1 for the skeleton root */
    mat4x4 *base_bind = joint >= 0 ? &model->joints[joint].bind : &model->root_pose;
    vec3 base_trans, base_scale;
    mat4x4_decompose(*base_bind, base_trans, chain->base_rot, base_scale);

    for (size_t k = 0; k < nr; k++) {
        model3d_joint_bind_local(model, chain->joints[k], k ? chain->joints[k - 1] : joint,
                                 chain->rest_trans[k], chain->rest_rot[k], chain->rest_scale[k]);
        vec3_dup(chain->rest_points[k], model->joints[chain->joints[k]].bind[3]);
    }

    for (size_t k = 0; k < nr - 1; k++) {
        vec3 seg;

        vec3_sub(seg, chain->rest_points[k + 1], chain->rest_points[k]);
        chain->lengths[k] = vec3_len(seg);
        if (chain->lengths[k] <= 0.0f)  goto err_args;

        /* the segment in the joint's rotated frame: scale distorts it */
        for (int c = 0; c < 3; c++) seg[c] = chain->rest_scale[k][c] * chain->rest_trans[k + 1][c];
        if (vec3_len(seg) <= 0.0f)  goto err_args;
        vec3_norm(chain->rest_dir[k], seg);
    }

    return CERR_OK;

err_nomem:
    ik_chain_done(chain);
    return CERR_NOMEM;

err_args:
    ik_chain_done(chain);
    return CERR_INVALID_ARGUMENTS;
}

void ik_chain_done(ik_chain *chain)
{
    mem_free(chain->joints);
    mem_free(chain->points);
    mem_free(chain->lengths);
    mem_free(chain->rest_points);
    mem_free(chain->rest_dir);
    mem_free(chain->rest_rot);
    mem_free(chain->rest_trans);
    mem_free(chain->rest_scale);
    *chain = (ik_chain){};
}

cerr _ik_chain_solve(ik_chain *chain, const ik_solve_opts *opts)
{
    if (!chain->e || !chain->nr)    return CERR_INVALID_ARGUMENTS;

    /* solve from rest: deterministic, no drift across frames */
    memcpy(chain->points, chain->rest_points, sizeof(*chain->points) * chain->nr);

    fabrik_solve_opts fabrik_opts = {
        .points         = chain->points,
        .lengths        = chain->lengths,
        .nr             = chain->nr,
        .max_iterations = opts->max_iterations,
        .tolerance      = opts->tolerance,
    };
    vec3_dup(fabrik_opts.target, opts->target);
    vec3_dup(fabrik_opts.pole, opts->pole);
    CERR_RET_CERR(_fabrik_solve(&fabrik_opts));

    /*
     * The roll anchor is the bend plane normal, not the pole itself: the
     * pole is the one direction the bow guarantees mid-chain segments
     * will approach, and projecting it onto a near-parallel segment's
     * perpendicular plane flips the reference around, twisting the chain
     * violently. The normal, on the other hand, is the direction the
     * segments never take while the chain stays in its bend plane.
     */
    bool pole_roll = vec3_len(opts->pole) > 0.0f;
    vec3 ref_axis = { 0.0f, 0.0f, 0.0f };
    if (pole_roll) {
        vec3 pole, reach;

        vec3_norm(pole, opts->pole);
        vec3_sub(reach, opts->target, chain->rest_points[0]);
        vec3_norm_safe(reach, reach);
        vec3_mul_cross(ref_axis, reach, pole);

        /* a target on the pole axis defines no bend plane: fall back */
        if (vec3_len(ref_axis) < IK_POLE_EPS)   pole_roll = false;
        else                                    vec3_norm(ref_axis, ref_axis);
    }

    /*
     * Rebuild local rotations so every bone tracks its solved segment.
     * parent_rot accumulates the new global rotations, bind_rot the bind
     * pose ones; carry_* hold the roll reference across normal-parallel
     * segments.
     */
    quat parent_rot, bind_rot;
    vec4_dup(parent_rot, chain->base_rot);
    vec4_dup(bind_rot, chain->base_rot);

    vec3 carry_rest = {}, carry_want = {};

    for (size_t k = 0; k < chain->nr; k++) {
        struct joint *j = &chain->e->joints[chain->joints[k]];

        vec3_dup(j->translation, chain->rest_trans[k]);
        vec3_dup(j->scale, chain->rest_scale[k]);

        /* the tip has no segment to aim, it just rides its parent */
        if (k == chain->nr - 1) {
            vec4_dup(j->rotation, chain->rest_rot[k]);
            break;
        }

        quat tmp;
        quat_mul(tmp, bind_rot, chain->rest_rot[k]);
        vec4_dup(bind_rot, tmp);

        vec3 want_dir;
        vec3_sub(want_dir, chain->points[k + 1], chain->points[k]);
        vec3_norm(want_dir, want_dir);

        quat new_rot;
        bool aligned = false;

        if (pole_roll) {
            vec3 rest_dir, rest_ref, want_ref;

            vec3_sub(rest_dir, chain->rest_points[k + 1], chain->rest_points[k]);
            vec3_norm(rest_dir, rest_dir);

            /*
             * Build the rotation between the rest and the solved
             * frames, both referenced to the bend plane normal, and
             * apply it on top of the bind orientation. Roll then only
             * depends on the current pose, not on the path that led to
             * it.
             */
            if (frame_ref(rest_ref, rest_dir, ref_axis, carry_rest) &&
                frame_ref(want_ref, want_dir, ref_axis, carry_want)) {
                quat align;

                frame_rotation(align, rest_dir, rest_ref, want_dir, want_ref);
                quat_mul(new_rot, align, bind_rot);
                aligned = true;
            }
        }

        if (!aligned) {
            /*
             * No pole (or a pole-parallel segment): minimal arc from
             * where the bone points with the rest rotation under the
             * new parent, i.e. parallel transport
             */
            quat cand;
            quat_mul(cand, parent_rot, chain->rest_rot[k]);

            vec3 cur_dir;
            quat_mul_vec3(cur_dir, cand, chain->rest_dir[k]);

            quat delta;
            quat_from_vectors(delta, cur_dir, want_dir);
            quat_mul(new_rot, delta, cand);
        }

        quat_norm(new_rot, new_rot);

        quat inv_parent;
        quat_conj(inv_parent, parent_rot);
        quat_mul(j->rotation, inv_parent, new_rot);
        quat_norm(j->rotation, j->rotation);

        vec4_dup(parent_rot, new_rot);
    }

    entity3d_pose_update(chain->e);

    return CERR_OK;
}
