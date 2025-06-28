/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CAMERA_H__
#define __CLAP_CAMERA_H__

#include "model.h"
#include "view.h"

struct camera {
    transform_t         xform;
    struct view         view;
    entity3d            *bv;
    float               bv_volume;
    vec3                target;
    unsigned int        zoom;
    float               dist;
    float               yaw_delta;
    float               pitch_delta;
    vec4                frustum_corner[4];
#ifndef CONFIG_FINAL
    vec3                debug_corner[4];
    vec3                debug_target;
#endif /* CONFIG_FINAL */
};

void camera_move(struct camera *c, unsigned long fps);
void camera_reset_movement(struct camera *c);
void camera_add_pitch(struct camera *c, float delta);
void camera_add_yaw(struct camera *c, float delta);
void camera_update(struct camera *c, struct scene *s);
bool camera_has_moved(struct camera *c);
#ifndef CONFIG_FINAL
void debug_camera_action(struct camera *c);
#else
static inline void debug_camera_action(struct camera *c) {}
#endif /* CONFIG_FINAL */

#endif
