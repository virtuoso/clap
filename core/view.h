/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_VIEW_H__
#define __CLAP_VIEW_H__

#include "shader_constants.h"

struct view {
    struct subview {
        mat4x4              view_mx;
        mat4x4              inv_view_mx;
        mat4x4              proj_mx;
        vec4                frustum_planes[6];
        vec4                frustum_corners[8];
        float               near_plane;
        float               far_plane;
    } main;
    struct subview          subview[CASCADES_MAX];
#ifndef CONFIG_FINAL
    struct subview          debug_subview[CASCADES_MAX];
#endif /* CONFIG_FINAL */
    float                   divider[CASCADES_MAX];
    float                   fov;
    float                   aspect;
};

typedef struct entity3d entity3d;
void view_update_perspective_projection(struct view *view, int width, int height, float zoom);
void view_update_from_angles(struct view *view, vec3 eye, float pitch, float yaw, float roll);
void view_update_from_frustum(struct view *view, struct view *src, vec3 dir, float near_backup, bool z_reverse);
void view_calc_frustum(struct view *view);
bool view_entity_in_frustum(struct view *view, entity3d *e);

#endif /* __CLAP_VIEW_H__ */
