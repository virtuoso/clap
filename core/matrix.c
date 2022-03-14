// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "matrix.h"

void mx_set_identity(struct matrix4f *m)
{
    int i;

    memset(m, 0, sizeof(*m));
    for (i = 0; i < array_size(m->cell); i += 5)
        m->cell[i] = 1.0;
}

void mx_translate(struct matrix4f *m, float translation[3])
{
    m->cell[3] = translation[0];
    m->cell[7] = translation[1];
    m->cell[11] = translation[2];
}

void mx_scale(struct matrix4f *m, float scale)
{
    m->cell[0] *= scale;
    m->cell[5] *= scale;
    m->cell[10] *= scale;
}

struct matrix4f *mx_new(void)
{
    struct matrix4f *m = malloc(sizeof(*m));
    if (!m)
        return NULL;

    mx_set_identity(m);

    return m;
}
//https://github.com/datenwolf/linmath.h
struct matrix4f *
transmx_new(float *translation, float rx, float ry, float rz, float scale)
{
    struct matrix4f *m;

    m = mx_new();
    if (translation)
        mx_translate(m, translation);
    mx_scale(m, scale);

    return m;
}

struct matrix4f *
viewmx_new(float *translation, float rx, float ry, float rz, float scale)
{
    struct matrix4f *m;

    m = mx_new();
    mx_scale(m, scale);
    if (translation)
        mx_translate(m, translation);

    return m;
}

struct matrix4f *
projmx_new(float translation[3], float rx, float ry, float rz, float scale)
{
    struct matrix4f *m;

    m = mx_new();
    mx_translate(m, translation);
    mx_scale(m, scale);

    return m;
}

