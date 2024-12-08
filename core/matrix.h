/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_MATRIX_H__
#define __CLAP_MATRIX_H__

#include <math.h>
#include "linmath.h"

struct matrix4f {
    union {
        float cell[16];
        mat4x4 m;
    };
};

void mx_set_identity(struct matrix4f *m);
void mx_translate(struct matrix4f *m, float translation[3]);
void mx_scale(struct matrix4f *m, float scale);
struct matrix4f *mx_new(void);
struct matrix4f *transmx_new(float *translation, float rx, float ry, float rz, float scale);
static inline float to_radians(float degrees)
{
    return degrees * M_PI / 180.0;
}

static inline float to_degrees(float radians)
{
    return radians / M_PI * 180.0;
}

#endif /* __CLAP_MATRIX_H__ */
