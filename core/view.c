// SPDX-License-Identifier: Apache-2.0
#include "model.h"
#include "view.h"

void view_update_from_angles(struct view *view, vec3 eye, float pitch, float yaw, float roll)
{
    mat4x4_identity(view->view_mx.m);
    mat4x4_rotate_X(view->view_mx.m, view->view_mx.m, to_radians(pitch));
    mat4x4_rotate_Y(view->view_mx.m, view->view_mx.m, to_radians(yaw));
    mat4x4_rotate_Z(view->view_mx.m, view->view_mx.m, to_radians(roll));
    mat4x4_translate_in_place(view->view_mx.m, -eye[0], -eye[1], -eye[2]);

    mat4x4_invert(view->inv_view_mx.m, view->view_mx.m);
}

void view_calc_frustum(struct view *view)
{
    /* They're not really MVPs, since there's no M matrices involved */
    mat4x4 mvp, trans, invmvp;
    vec4 corners[] = {
        { -1, -1, -1, 1 }, { 1, -1, -1, 1 },
        { 1, 1, -1, 1 }, { -1, 1, -1, 1 },
        { -1, -1, 1, 1 }, { 1, -1, 1, 1 },
        { 1, 1, 1, 1 }, { -1, 1, 1, 1 }
    };
    int i = 0;

    mat4x4_mul(mvp, view->proj_mx->m, view->view_mx.m);
    mat4x4_transpose(trans, mvp);
    mat4x4_invert(invmvp, mvp);

    /* frustum planes */
    vec4_add(view->frustum_planes[0], trans[3], trans[0]);
    vec4_sub(view->frustum_planes[1], trans[3], trans[0]);
    vec4_add(view->frustum_planes[2], trans[3], trans[1]);
    vec4_sub(view->frustum_planes[3], trans[3], trans[1]);
    vec4_add(view->frustum_planes[4], trans[3], trans[2]);
    vec4_sub(view->frustum_planes[5], trans[3], trans[2]);

    /* frustum corners */
    for (i = 0; i < 8; i++) {
        vec4 q;

        mat4x4_mul_vec4(q, invmvp, corners[i]);
        vec4_scale(view->frustum_corners[i], q, 1.f / q[3]);
    }
}

bool view_entity_in_frustum(struct view *view, struct entity3d *e)
{
    vec3 min, max;
    int i;

    entity3d_aabb_min(e, min);
    entity3d_aabb_max(e, max);

    for (i = 0; i < 6; i++) {
        int r = 0;
        vec4 v;

        vec4_setup(v, min[0], min[1], min[2], 1.0);
        r += (vec4_mul_inner(view->frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, max[0], min[1], min[2], 1.0);
        r += (vec4_mul_inner(view->frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, min[0], max[1], min[2], 1.0);
        r += (vec4_mul_inner(view->frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, max[0], max[1], min[2], 1.0);
        r += (vec4_mul_inner(view->frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, min[0], min[1], max[2], 1.0);
        r += (vec4_mul_inner(view->frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, max[0], min[1], max[2], 1.0);
        r += (vec4_mul_inner(view->frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, min[0], max[1], max[2], 1.0);
        r += (vec4_mul_inner(view->frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, max[0], max[1], max[2], 1.0);
        r += (vec4_mul_inner(view->frustum_planes[i], v) < 0.0) ? 1 : 0;
        if (r == 8)
            return false;
    }

    int r = 0;
    for (r = 0, i = 0; i < 8; i++) r += view->frustum_corners[i][0] > max[0] ? 1 : 0; if (r == 8) return false;
    for (r = 0, i = 0; i < 8; i++) r += view->frustum_corners[i][0] < min[0] ? 1 : 0; if (r == 8) return false;
    for (r = 0, i = 0; i < 8; i++) r += view->frustum_corners[i][1] > max[1] ? 1 : 0; if (r == 8) return false;
    for (r = 0, i = 0; i < 8; i++) r += view->frustum_corners[i][1] < min[1] ? 1 : 0; if (r == 8) return false;
    for (r = 0, i = 0; i < 8; i++) r += view->frustum_corners[i][2] > max[2] ? 1 : 0; if (r == 8) return false;
    for (r = 0, i = 0; i < 8; i++) r += view->frustum_corners[i][2] < min[2] ? 1 : 0; if (r == 8) return false;

    return true;
}
