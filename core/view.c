// SPDX-License-Identifier: Apache-2.0
#include "model.h"
#include "view.h"
#include "shader.h"
#include "ui-debug.h"

static float near_factor = 0.0, far_factor = 1.0, frustum_extra = 10.0;

static void subview_calc_frustum(struct subview *subview);

static void view_update_perspective_subviews(struct view *view)
{
    static const float dividers[CASCADES_MAX - 1] = { 25, 70, 150 };
    int max = array_size(view->subview);
    int i;

    view->subview[0].near_plane = view->main.near_plane;
    for (i = 0; i < max - 1; i++) {
        struct subview *sv = &view->subview[i];

        view->divider[i] = dividers[i];
        sv->far_plane    = view->divider[i];
        view->subview[i + 1].near_plane = sv->far_plane;
    }
    view->divider[i]           = view->main.far_plane;
    view->subview[i].far_plane = view->main.far_plane;

    for (i = 0; i < max; i++) {
        struct subview *sv = &view->subview[i];

        mat4x4_dup(sv->view_mx.m, view->main.view_mx.m);
        mat4x4_dup(sv->inv_view_mx.m, view->main.inv_view_mx.m);
        mat4x4_perspective(sv->proj_mx.m, view->fov, view->aspect, sv->near_plane, sv->far_plane);
        subview_calc_frustum(sv);
    }
}

#ifndef CONFIG_FINAL
static void view_debug_begin(void)
{
   debug_module *dbgm = ui_igBegin(DEBUG_FRUSTUM_VIEW, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display || !dbgm->unfolded)
        return;

    igSliderFloat("frustum extra", &frustum_extra, 1.0, 50.0, "%.1f", ImGuiSliderFlags_ClampOnInput);
    igSliderFloat("near plane", &near_factor, -10.0, 10.0, "%.1f", ImGuiSliderFlags_ClampOnInput);
    igSliderFloat("far plane", &far_factor, -10.0, 10.0, "%.1f", ImGuiSliderFlags_ClampOnInput);
}

static void subview_debug(struct subview *dst, float *light_pos,
                          float *_aabb_min, float *_aabb_max,
                          float *aabb_min, float *aabb_max)
{
    debug_module *dbgm = ui_debug_module(DEBUG_FRUSTUM_VIEW);

    if (!dbgm->display || !dbgm->unfolded)
        return;

    vec4 light_pos_world;
    mat4x4_mul_vec4(light_pos_world, dst->inv_view_mx.m, light_pos);

    igText("projection matrix");
    ui_igMat4x4(dst->proj_mx.m, "projection");
    igText("view matrix");
    ui_igMat4x4(dst->view_mx.m, "view");
    // igText("_aabb_min[2]: %f dir: %f", _aabb_min[2], vec3_len(dir));
    ui_igVecTableHeader("AABB", 4);
    ui_igVecRow(light_pos, 4, "light pos");
    ui_igVecRow(light_pos_world, 4, "light pos world");
    ui_igVecRow(_aabb_min, 4, "_aabb_min");
    ui_igVecRow(_aabb_max, 4, "_aabb_max");
    ui_igVecRow(aabb_min, 4, "aabb_min");
    ui_igVecRow(aabb_max, 4, "aabb_max");
    igEndTable();
}

static void view_frustum_debug(struct view *src, int idx)
{
    debug_module *dbgm = ui_debug_module(DEBUG_FRUSTUM_VIEW);

    if (!dbgm->display || !dbgm->unfolded)
        return;

    igSeparator();
    igText("subview %d near/far_plane: %f .. %f", idx,
           src->subview[idx].near_plane, src->subview[idx].far_plane);
    // ui_igVecTableHeader("in light space", 4);
    // int i;
    // for (i = 0; i < 8; i++)
    //     ui_igVecRow(view->subview[v].frustum_corners[i], 4, "%d: light corner %d", v, i);
    // for (i = 0; i < 8; i++)
    //     ui_igVecRow(src->subview[v].frustum_corners[i], 4, "%d: camera corner %d", v, i);
    // igEndTable();
}

static void view_debug_end(void)
{
    ui_igEnd(DEBUG_FRUSTUM_VIEW);
}
#else
static inline void view_debug_begin(void) {}
static inline  void subview_debug(struct subview *dst, float *light_pos,
                                  float *_aabb_min, float *_aabb_max,
                                  float *aabb_min, float *aabb_max) {}
static inline void view_frustum_debug(struct view *src, int idx) {}
static inline void view_debug_end(void) {}
#endif /* CONFIG_FINAL */

static void subview_projection_update(struct subview *dst, struct subview *src)
{
    vec4 _aabb_min = { INFINITY, INFINITY, INFINITY, 1 }, _aabb_max = { -INFINITY, -INFINITY, -INFINITY, 1 };
    int i, j;

    for (i = 0; i < 8; i++) {
        vec4 corner;
        mat4x4_mul_vec4(corner, dst->view_mx.m, src->frustum_corners[i]);

        for (j = 0; j < 3; j++) {
            _aabb_min[j] = fminf(_aabb_min[j], corner[j]);
            _aabb_max[j] = fmaxf(_aabb_max[j], corner[j]);
        }
    }

    if (_aabb_min[2] < 0)
        _aabb_min[2] *= frustum_extra;
    else
        _aabb_min[2] /= frustum_extra;
    // if (aabb_max[2] < 0)
    //     aabb_max[2] /= FRUSTUM_EXTRA;
    // else
    //     aabb_max[2] *= FRUSTUM_EXTRA;

    vec3 near_bottom_left = { _aabb_min[0], _aabb_min[1], _aabb_min[2] };
    vec3 near_top_right = { _aabb_max[0], _aabb_max[1], _aabb_min[2] };
    vec4 light_pos = { 0, 0, 0, 1 };
    vec3_add(light_pos, near_bottom_left, near_top_right);
    vec3_scale(light_pos, light_pos, 0.5);
    /* this is the same as translate_in_place by light_pos_world, but quicker */
    dst->view_mx.m[3][0] = -light_pos[0];
    dst->view_mx.m[3][1] = -light_pos[1];
    dst->view_mx.m[3][2] = -light_pos[2];
    mat4x4_invert(dst->inv_view_mx.m, dst->view_mx.m);

    vec4 aabb_min = { INFINITY, INFINITY, INFINITY, 1 }, aabb_max = { -INFINITY, -INFINITY, -INFINITY, 1 };

    for (i = 0; i < 8; i++) {
        vec4 corner;
        mat4x4_mul_vec4(corner, dst->view_mx.m, src->frustum_corners[i]);

        for (j = 0; j < 3; j++) {
            aabb_min[j] = fminf(aabb_min[j], corner[j]);
            aabb_max[j] = fmaxf(aabb_max[j], corner[j]);
        }
    }

    mat4x4_ortho(dst->proj_mx.m, aabb_min[0], aabb_max[0], aabb_min[1], aabb_max[1],
                 aabb_min[2] * near_factor, aabb_max[2] * far_factor);
    subview_calc_frustum(dst);
    subview_debug(dst, light_pos, _aabb_min, _aabb_max, aabb_min, aabb_max);
}

static void view_projection_update(struct view *view, struct view *src)
{
    int v;

    view_debug_begin();
    for (v = 0; v < array_size(view->subview); v++) {
        view_frustum_debug(src, v);
        subview_projection_update(&view->subview[v], &src->subview[v]);
    }

    view_debug_end();

    view_calc_frustum(view);
}

static void subview_update_from_angles(struct subview *sv, vec3 eye, float pitch, float yaw, float roll)
{
    mat4x4_identity(sv->view_mx.m);
    mat4x4_rotate_X(sv->view_mx.m, sv->view_mx.m, to_radians(pitch));
    mat4x4_rotate_Y(sv->view_mx.m, sv->view_mx.m, to_radians(yaw));
    mat4x4_rotate_Z(sv->view_mx.m, sv->view_mx.m, to_radians(roll));
    mat4x4_translate_in_place(sv->view_mx.m, -eye[0], -eye[1], -eye[2]);

    mat4x4_invert(sv->inv_view_mx.m, sv->view_mx.m);
}

void view_update_from_angles(struct view *view, vec3 eye, float pitch, float yaw, float roll)
{
    /* XXX: do the main subview first and just mat4x4_dup() them into subviews */
    subview_update_from_angles(&view->main, eye, pitch, yaw, roll);
    view_update_perspective_subviews(view);
}

void view_update_perspective_projection(struct view *view, int width, int height)
{
    view->aspect = (float)width / (float)height;
    mat4x4_perspective(view->main.proj_mx.m, view->fov, view->aspect,
                       view->main.near_plane, view->main.far_plane);
    view_update_perspective_subviews(view);
}

static void subview_update_from_target(struct subview *subview, vec3 eye, vec3 target)
{
    vec3 up = { 0.0, 1.0, 0.0 };

    mat4x4_look_at(subview->view_mx.m, eye, target, up);
    mat4x4_invert(subview->inv_view_mx.m, subview->view_mx.m);
}

void view_update_from_target(struct view *view, vec3 eye, vec3 target)
{
    int i;

    for (i = 0; i < array_size(view->subview); i++)
        subview_update_from_target(&view->subview[i], eye, target);

    subview_update_from_target(&view->main, eye, target);
}

void view_update_from_frustum(struct view *view, vec3 dir, struct view *src)
{
    vec3 center = { -dir[0], -dir[1], -dir[2] };
    vec3 eye = {};

    view_update_from_target(view, eye, center);
    view_projection_update(view, src);
}

static void subview_calc_frustum(struct subview *subview)
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

    mat4x4_mul(mvp, subview->proj_mx.m, subview->view_mx.m);
    mat4x4_transpose(trans, mvp);
    mat4x4_invert(invmvp, mvp);

    /* frustum planes */
    vec4_add(subview->frustum_planes[0], trans[3], trans[0]);
    vec4_sub(subview->frustum_planes[1], trans[3], trans[0]);
    vec4_add(subview->frustum_planes[2], trans[3], trans[1]);
    vec4_sub(subview->frustum_planes[3], trans[3], trans[1]);
    vec4_add(subview->frustum_planes[4], trans[3], trans[2]);
    vec4_sub(subview->frustum_planes[5], trans[3], trans[2]);

    /* frustum corners */
    for (i = 0; i < 8; i++) {
        vec4 q;

        mat4x4_mul_vec4(q, invmvp, corners[i]);
        vec4_scale(subview->frustum_corners[i], q, 1.f / q[3]);
    }
}

void view_calc_frustum(struct view *view)
{
    return subview_calc_frustum(&view->main);
}

bool view_entity_in_frustum(struct view *view, entity3d *e)
{
    vec3 min, max;
    int i;

    entity3d_aabb_min(e, min);
    entity3d_aabb_max(e, max);

    for (i = 0; i < 6; i++) {
        int r = 0;
        vec4 v;

        vec4_setup(v, min[0], min[1], min[2], 1.0);
        r += (vec4_mul_inner(view->main.frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, max[0], min[1], min[2], 1.0);
        r += (vec4_mul_inner(view->main.frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, min[0], max[1], min[2], 1.0);
        r += (vec4_mul_inner(view->main.frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, max[0], max[1], min[2], 1.0);
        r += (vec4_mul_inner(view->main.frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, min[0], min[1], max[2], 1.0);
        r += (vec4_mul_inner(view->main.frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, max[0], min[1], max[2], 1.0);
        r += (vec4_mul_inner(view->main.frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, min[0], max[1], max[2], 1.0);
        r += (vec4_mul_inner(view->main.frustum_planes[i], v) < 0.0) ? 1 : 0;
        vec4_setup(v, max[0], max[1], max[2], 1.0);
        r += (vec4_mul_inner(view->main.frustum_planes[i], v) < 0.0) ? 1 : 0;
        if (r == 8)
            return false;
    }

    int r = 0;
    for (r = 0, i = 0; i < 8; i++) r += view->main.frustum_corners[i][0] > max[0] ? 1 : 0; if (r == 8) return false;
    for (r = 0, i = 0; i < 8; i++) r += view->main.frustum_corners[i][0] < min[0] ? 1 : 0; if (r == 8) return false;
    for (r = 0, i = 0; i < 8; i++) r += view->main.frustum_corners[i][1] > max[1] ? 1 : 0; if (r == 8) return false;
    for (r = 0, i = 0; i < 8; i++) r += view->main.frustum_corners[i][1] < min[1] ? 1 : 0; if (r == 8) return false;
    for (r = 0, i = 0; i < 8; i++) r += view->main.frustum_corners[i][2] > max[2] ? 1 : 0; if (r == 8) return false;
    for (r = 0, i = 0; i < 8; i++) r += view->main.frustum_corners[i][2] < min[2] ? 1 : 0; if (r == 8) return false;

    return true;
}
