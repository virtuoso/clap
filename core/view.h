/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_VIEW_H__
#define __CLAP_VIEW_H__

#include "matrix.h"

struct view {
    struct matrix4f     view_mx;
    struct matrix4f     inv_view_mx;
    struct matrix4f     _proj_mx;
    struct matrix4f     *proj_mx;
    vec4                frustum_planes[6];
    vec4                frustum_corners[8];
};

struct entity3d;
void view_update_from_angles(struct view *view, vec3 eye, float pitch, float yaw, float roll);
void view_calc_frustum(struct view *view);
bool view_entity_in_frustum(struct view *view, struct entity3d *e);

#endif /* __CLAP_VIEW_H__ */
