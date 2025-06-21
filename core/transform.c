// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "transform.h"
#undef IMPLEMENTOR

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

void transform_translate(transform_t *xform, mat4x4 m)
{
    mat4x4_translate_in_place(m, xform->pos[0], xform->pos[1], xform->pos[2]);
}
