// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "transform.h"
#undef IMPLEMENTOR

#include "util.h"

void transform_init(transform_t *xform)
{
    memset(xform, 0, sizeof(*xform));
    quat_identity(xform->rotation);
}

void transform_clone(transform_t *dest, transform_t *src)
{
    memcpy(dest, src, sizeof(*dest));
    transform_set_updated(dest);
}

void transform_set_updated(transform_t *xform)
{
    xform->updated = true;
}

void transform_clear_updated(transform_t *xform)
{
    xform->updated = false;
}

bool transform_is_updated(transform_t *xform)
{
    return xform->updated;
}

void transform_set_pos(transform_t *xform, const vec3 pos)
{
    vec3_dup(xform->pos, pos);
    transform_set_updated(xform);
}

const float *transform_move(transform_t *xform, const vec3 off)
{
    vec3_add(xform->pos, xform->pos, off);
    transform_set_updated(xform);

    return xform->pos;
}

const float *transform_pos(transform_t *xform, float *pos)
{
    if (pos)
        vec3_dup(pos, xform->pos);

    return xform->pos;
}

void transform_translate_mat4x4(transform_t *xform, mat4x4 m)
{
    mat4x4_translate_in_place(m, xform->pos[0], xform->pos[1], xform->pos[2]);
}

void transform_set_angles(transform_t *xform, const float angles[3], bool degrees)
{
    vec3 rads;

    for (int i = 0; i < 3; i++)
        rads[i] = degrees ?
            to_radians(clamp_degrees(angles[i])) :
            clamp_radians(angles[i]);
    quat_from_euler_xyz(xform->rotation, rads[0], rads[1], rads[2]);

    transform_set_updated(xform);
}

void transform_set_quat(transform_t *xform, const quat quat)
{
    vec4_dup(xform->rotation, quat);
    transform_set_updated(xform);
}

void transform_rotate_axis(transform_t *xform, vec3 axis, float angle, bool degrees)
{
    vec3 up = { 0.0, 1.0, 0.0 };
    quat r, id, src;

    memcpy(src, xform->rotation, sizeof(src));
    quat_identity(id);
    quat_from_axis_angle(r, axis, degrees ? to_radians(angle) : angle);
    if (vec3_mul_inner(axis, up) > 0.9)
        quat_mul(xform->rotation, r, src);
    else
        quat_mul(xform->rotation, src, r);
    transform_set_updated(xform);
}

const float *transform_rotation_quat(transform_t *xform)
{
    return xform->rotation;
}

void transform_rotation(transform_t *xform, float angles[3], bool degrees)
{
    quat_to_euler_xyz(xform->rotation, &angles[0], &angles[1], &angles[2]);
    if (degrees)
        for (int i = 0; i < 3; i++)
            angles[i] = to_degrees(angles[i]);
}

void transform_rotate_vec3(transform_t *xform, vec3 v)
{
    vec3 r;
    quat_mul_vec3(r, xform->rotation, v);
    vec3_dup(v, r);
}

void transform_orbit(transform_t *xform, vec3 target, float len)
{
    vec4 start = { 0.0, 0.0, len, 1.0 }, off;

    transform_rotate_vec3(xform, start);
    vec3_add(off, start, target);
    transform_set_pos(xform, off);
}

void transform_rotate_mat4x4(transform_t *xform, mat4x4 m)
{
    mat4x4 r;
    mat4x4_from_quat(r, xform->rotation);
    mat4x4_mul(m, m, r);
}

void transform_view_mat4x4(transform_t *xform, mat4x4 m)
{
    mat4x4_identity(m);
    transform_rotate_mat4x4(xform, m);
    mat4x4_transpose_mat3x3(m);
    mat4x4_translate_in_place(m, -xform->pos[0], -xform->pos[1], -xform->pos[2]);
}
