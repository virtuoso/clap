// SPDX-License-Identifier: Apache-2.0
#include "model.h"
#include "view.h"
#include "shader.h"

static float near_factor = 0.0, far_factor = 1.0, frustum_extra = 10.0;

static void view_projection_update(struct view *view, struct view *src)
{
    vec4 _aabb_min = { INFINITY, INFINITY, INFINITY, 1 }, _aabb_max = { -INFINITY, -INFINITY, -INFINITY, 1 };
    int i, j;

    for (i = 0; i < 8; i++) {
        vec4 corner;
        mat4x4_mul_vec4(corner, view->view_mx.m, src->frustum_corners[i]);

        for (j = 0; j < 3; j++) {
            _aabb_min[j] = fminf(_aabb_min[j], corner[j]);
            _aabb_max[j] = fmaxf(_aabb_max[j], corner[j]);
        }
    }

    if (_aabb_min[2] < 0)
        _aabb_min[2] *= frustum_extra;
    else
        _aabb_min[2] /= frustum_extra;

    vec3 near_bottom_left = { _aabb_min[0], _aabb_min[1], _aabb_min[2] };
    vec3 near_top_right = { _aabb_max[0], _aabb_max[1], _aabb_min[2] };
    vec4 light_pos = { 0, 0, 0, 1 };
    vec3_add(light_pos, near_bottom_left, near_top_right);
    vec3_scale(light_pos, light_pos, 0.5);
    vec4 light_pos_world;
    mat4x4_mul_vec4(light_pos_world, view->inv_view_mx.m, light_pos);
    /* this is the same as translate_in_place by light_pos_world, but quicker */
    view->view_mx.m[3][0] = -light_pos[0];
    view->view_mx.m[3][1] = -light_pos[1];
    view->view_mx.m[3][2] = -light_pos[2];
    mat4x4_invert(view->inv_view_mx.m, view->view_mx.m);

    vec4 aabb_min = { INFINITY, INFINITY, INFINITY, 1 }, aabb_max = { -INFINITY, -INFINITY, -INFINITY, 1 };

    for (i = 0; i < 8; i++) {
        vec4 corner;
        mat4x4_mul_vec4(corner, view->view_mx.m, src->frustum_corners[i]);

        for (j = 0; j < 3; j++) {
            aabb_min[j] = fminf(aabb_min[j], corner[j]);
            aabb_max[j] = fmaxf(aabb_max[j], corner[j]);
        }
    }

    mat4x4_ortho(view->proj_mx.m, aabb_min[0], aabb_max[0], aabb_min[1], aabb_max[1],
                 aabb_min[2] * near_factor, aabb_max[2] * far_factor);
    view_calc_frustum(view);
}

void view_update_from_angles(struct view *view, vec3 eye, float pitch, float yaw, float roll)
{
    mat4x4_identity(view->view_mx.m);
    mat4x4_rotate_X(view->view_mx.m, view->view_mx.m, to_radians(pitch));
    mat4x4_rotate_Y(view->view_mx.m, view->view_mx.m, to_radians(yaw));
    mat4x4_rotate_Z(view->view_mx.m, view->view_mx.m, to_radians(roll));
    mat4x4_translate_in_place(view->view_mx.m, -eye[0], -eye[1], -eye[2]);

    mat4x4_invert(view->inv_view_mx.m, view->view_mx.m);
}

void view_update_perspective_projection(struct view *view, int width, int height)
{
    view->aspect = (float)width / (float)height;
    mat4x4_perspective(view->proj_mx.m, view->fov, view->aspect,
                       view->near_plane, view->far_plane);
}

void view_update_from_target(struct view *view, vec3 eye, vec3 target)
{
    vec3 up = { 0.0, 1.0, 0.0 };

    mat4x4_look_at(view->view_mx.m, eye, target, up);
    mat4x4_invert(view->inv_view_mx.m, view->view_mx.m);
}

void view_update_from_frustum(struct view *view, vec3 dir, struct view *src)
{
    vec3 up = { 0.0, 1.0, 0.0 }, center = { -dir[0], -dir[1], -dir[2] };
    int i;

    vec3 eye = {};
    view_update_from_target(view, eye, center);
    view_projection_update(view, src);
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

    mat4x4_mul(mvp, view->proj_mx.m, view->view_mx.m);
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
