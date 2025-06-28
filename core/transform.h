/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_TRANSFORM_H__
#define __CLAP_TRANSFORM_H__

#include "linmath.h"
#include "typedef.h"

TYPE(transform,
    vec3    pos;
    quat    rotation;
    float   scale;
    bool    updated;
);

void transform_init(transform_t *xform);
void transform_clone(transform_t *dest, transform_t *src);
void transform_set_updated(transform_t *xform);
void transform_clear_updated(transform_t *xform);
bool transform_is_updated(transform_t *xform);
void transform_set_pos(transform_t *xform, const vec3 pos);
const float *transform_move(transform_t *xform, const vec3 off);
const float *transform_pos(transform_t *xform, float *pos);
void transform_translate_mat4x4(transform_t *xform, mat4x4 m);
void transform_set_angles(transform_t *xform, const float angles[3], bool degrees);
void transform_set_quat(transform_t *xform, const quat quat);
void transform_rotate_axis(transform_t *xform, vec3 axis, float angle, bool degrees);
void transform_rotation(transform_t *xform, float angles[3], bool degrees);
const float *transform_rotation_quat(transform_t *xform);
void transform_rotate_vec3(transform_t *xform, vec3 v);
void transform_orbit(transform_t *xform, vec3 target, float len);
void transform_rotate_mat4x4(transform_t *xform, mat4x4 m);
void transform_view_mat4x4(transform_t *xform, mat4x4 m);

#endif /* __CLAP_TRANSFORM_H__ */
