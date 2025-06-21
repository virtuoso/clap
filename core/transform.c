// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "transform.h"
#undef IMPLEMENTOR

#include "util.h"

void transform_set_pos(transform_t *xform, const vec3 pos)
{
    vec3_dup(xform->pos, pos);
}

const float *transform_move(transform_t *xform, const vec3 off)
{
    vec3_add(xform->pos, xform->pos, off);

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
    vec3_dup(
        xform->angles,
        degrees ? (vec3) {
            to_radians(angles[0]),
            to_radians(angles[1]),
            to_radians(angles[2])
        } : angles
    );
}

void transform_rotation(transform_t *xform, float angles[3], bool degrees)
{
    if (degrees)
        for (int i = 0; i < 3; i++)
            angles[i] = to_degrees(xform->angles[i]);
    else
        vec3_dup(angles, xform->angles);
}

void transform_rotate_mat4x4(transform_t *xform, mat4x4 m)
{
    mat4x4_rotate_X(m, m, xform->angles[0]);
    mat4x4_rotate_Y(m, m, xform->angles[1]);
    mat4x4_rotate_Z(m, m, xform->angles[2]);
}
