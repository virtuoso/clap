// SPDX-License-Identifier: Apache-2.0
#include "common.h"
#include "model.h"
#include "physics-common.h"

phys_capsule phys_capsule_size(entity3d *e, double radius, double offset, double length)
{
    float X = entity3d_aabb_X(e), Y = entity3d_aabb_Y(e), Z = entity3d_aabb_Z(e);
    phys_capsule cap = { .axis = xmax3(X, Y, Z) };

    switch (cap.axis) {
        case 0:
        case 1:
            /*
             * Upright
             */
            cap.radius  = radius ? radius : min3(X, Y, Z) / 2;
            cap.length  = length ? length : max(Y / 2 - cap.radius * 2, 0);
            cap.yoffset = offset ? offset : Y / 2;
            cap.ray_off = cap.radius + cap.length / 2;
            break;
        case 2:
            /*
             * Puppy
             */
            cap.radius  = radius ? radius : X / 2;
            cap.length  = length ? length : Z - cap.radius * 2;
            cap.yoffset = offset ? offset : (Y - cap.radius * 2) / 2;
            cap.ray_off = cap.radius;
            break;
    }

    /*
     * XXX: The above logic is busted: ray length ends up zero.
     */

    return cap;
}

bool phys_limb_joint_valid(entity3d *e, int joint)
{
    return e->joints && joint >= 0 && (unsigned int)joint < e->txmodel->model->nr_joints;
}

vec3 *phys_limb_vx(entity3d *e, int joint, const vec3 *vx, size_t nr_vx)
{
    vec3 *local = mem_alloc(sizeof(*local), .nr = nr_vx, .fatal_fail = 1);
    mat4x4 *invmx = &e->txmodel->model->joints[joint].invmx;

    for (size_t i = 0; i < nr_vx; i++) {
        vec4 v = { vx[i][0], vx[i][1], vx[i][2], 1.0f }, l;

        mat4x4_mul_vec4(l, *invmx, v);
        vec3_dup(local[i], l);
    }

    return local;
}

void phys_limb_place(entity3d *e, int joint, const vec3 offset, vec3 pos, mat4x4 rot)
{
    vec4 o = { offset[0], offset[1], offset[2], 1.0f }, p;
    mat4x4 m;

    mat4x4_mul(m, e->joint_transforms[joint], e->txmodel->model->joints[joint].bind);
    mat4x4_mul(m, e->mx, m);
    mat4x4_mul_vec4(p, m, o);
    vec3_dup(pos, p);
    mat4x4_orthonormalize(rot, m);
}
