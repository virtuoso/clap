/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_INTERP_H__
#define __CLAP_INTERP_H__

#include <string.h>
#include "linmath.h"
#include "util.h"

/* Cosine interpolation between floats */
static inline float cos_interp(float a, float b, float blend)
{
    float theta = blend * M_PI;
    float  f = (1.f - cosf(theta)) / 2.f;
    return a * (1.f - f) + b * f;
}

/* Barrycentric interpolation between 3 vertices at @pos */
static inline float barrycentric(vec3 p1, vec3 p2, vec3 p3, vec2 pos)
{
    float det = (p2[2] - p3[2]) * (p1[0] - p3[0]) + (p3[0] - p2[0]) * (p1[2] - p3[2]);
    float l1  = ((p2[2] - p3[2]) * (pos[0] - p3[0]) + (p3[0] - p2[0]) * (pos[1] - p3[2])) / det;
    float l2  = ((p3[2] - p1[2]) * (pos[0] - p3[0]) + (p1[0] - p3[0]) * (pos[1] - p3[2])) / det;
    float l3  = 1.0f - l1 - l2;
    return l1 * p1[1] + l2 * p2[1] + l3 * p3[1];
}

/* Linear interpolation between 2 vectors */
static inline void vec3_interp(vec3 res, vec3 a, vec3 b, float fac)
{
    float rfac = 1.f - fac;

    res[0] = rfac * a[0] + fac * b[0];
    res[1] = rfac * a[1] + fac * b[1];
    res[2] = rfac * a[2] + fac * b[2];
}

/* Linear interpolation between 2 quaternions */
static inline void quat_interp(quat res, quat a, quat b, float fac)
{
    float dot = quat_inner_product(a, b);
    float rfac = 1.f - fac;

    if (dot < 0) {
        res[3] = rfac * a[3] - fac * b[3];
        res[0] = rfac * a[0] - fac * b[0];
        res[1] = rfac * a[1] - fac * b[1];
        res[2] = rfac * a[2] - fac * b[2];
    } else {
        res[3] = rfac * a[3] + fac * b[3];
        res[0] = rfac * a[0] + fac * b[0];
        res[1] = rfac * a[1] + fac * b[1];
        res[2] = rfac * a[2] + fac * b[2];
    }
    quat_norm(res, res);
}

/*
 * Spheric Linear Interpolation between quaternions
 * Lifted verbatim from
 * https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_007_Animations.html
 */
static inline void quat_slerp(quat res, quat a, quat b, float fac)
{
    float dot = quat_inner_product(a, b);
    quat _b;

    memcpy(_b, b, sizeof(_b));
    if (dot < 0.0) {
        int i;
        dot = -dot;
        for (i = 0; i < 4; i++) _b[i] = -b[i];
    }
    if (dot > 0.9995) {
        quat_interp(res, a, _b, fac);
        return;
    }

    float theta_0 = acos(dot);
    float theta = fac * theta_0;
    float sin_theta = sin(theta);
    float sin_theta_0 = sin(theta_0);

    float _rfac = cos(theta) - dot * sin_theta / sin_theta_0;
    float _fac = sin_theta / sin_theta_0;
    quat scaled_a, scaled_b;
    quat_scale(scaled_a, a, _rfac);
    quat_scale(scaled_b, _b, _fac);
    quat_add(res, scaled_a, scaled_b);
}

#endif /* __CLAP_INTERP_H__ */
