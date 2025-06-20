/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_TRANSFORM_H__
#define __CLAP_TRANSFORM_H__

#include "linmath.h"
#include "typedef.h"

TYPE(transform,
    vec3    pos;
);

void transform_set_pos(transform_t *xform, const vec3 pos);
float *transform_move(transform_t *xform, vec3 off);
float *transform_pos(transform_t *xform, float *pos);
void transform_translate(transform_t *xform, mat4x4 m);

#endif /* __CLAP_TRANSFORM_H__ */
