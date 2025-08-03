/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_VIEW_H__
#define __CLAP_VIEW_H__

#include "linmath.h"
#include "shader_constants.h"

/**
 * struct subview - a slice of a view frustum
 *
 * View frustum (slice or whole) representation with translation/projection
 * matrices, frustum planes/corners and near/far planes.
 */
struct subview {
    mat4x4  view_mx;            /** view matrix */
    mat4x4  inv_view_mx;        /** inverse view matrix */
    mat4x4  proj_mx;            /** projection matrix */
    vec4    frustum_planes[6];  /** view frustum planes */
    vec4    frustum_corners[8]; /** view frustum corners */
    float   near_plane;         /** near plane */
    float   far_plane;          /** far plane */
};

struct view {
    struct subview          main;
    struct subview          subview[CASCADES_MAX];
#ifndef CONFIG_FINAL
    struct subview          debug_subview[CASCADES_MAX];
#endif /* CONFIG_FINAL */
    float                   divider[CASCADES_MAX];
    float                   fov;
    float                   aspect;
    bool                    proj_update;
};

typedef struct entity3d entity3d;
void view_update_perspective_projection(struct view *view, int width, int height, float zoom);
void view_update_from_angles(struct view *view, vec3 eye, float pitch, float yaw, float roll);
void view_update_from_frustum(struct view *view, struct view *src, vec3 dir, float near_backup, bool z_reverse);
void view_calc_frustum(struct view *view);
bool view_entity_in_frustum(struct view *view, entity3d *e);

#endif /* __CLAP_VIEW_H__ */
