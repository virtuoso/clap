// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include "logger.h"
#include "xyarray.h"

struct xyzarray *xyzarray_new(ivec3 dim)
{
    struct xyzarray *xyz;

    CHECK(xyz = calloc(1, offsetof(struct xyzarray, arr[dim[0] * dim[1] * dim[2]])));
    xyz->dim[0] = dim[0];
    xyz->dim[1] = dim[1];
    xyz->dim[2] = dim[2];

    return xyz;
}

bool xyzarray_valid(struct xyzarray *xyz, ivec3 pos)
{
    int i;
    for (i = 0; i < 3; i++)
        if (pos[i] < 0 || pos[i] >= xyz->dim[i])
            return false;
    return true;
}

bool xyzarray_edgemost(struct xyzarray *xyz, ivec3 pos)
{
    int i;
    for (i = 0; i < 3; i++)
        if (pos[i] - 1 == 0 || pos[i] + 1 == xyz->dim[i])
            return true;
    return false;
}

int xyzarray_get(struct xyzarray *xyz, ivec3 pos)
{
    if (!xyzarray_valid(xyz, pos))
        return 0;
    return xyz->arr[pos[2] * xyz->dim[0] * xyz->dim[1] + pos[1] * xyz->dim[0] + pos[0]];
}

void xyzarray_set(struct xyzarray *xyz, ivec3 pos, int val)
{
    if (!xyzarray_valid(xyz, pos))
        return;
    xyz->arr[pos[2] * xyz->dim[0] * xyz->dim[1] + pos[1] * xyz->dim[0] + pos[0]] = val;
}

void xyzarray_print(struct xyzarray *xyz)
{
    char *line;
    int x, y, z;

    CHECK(line = calloc(xyz->dim[0] + 1, 1));
    for (z = 0; z < xyz->dim[2]; z++)
        for (y = 0; y < xyz->dim[1]; y++) {
            for (x = 0; x < xyz->dim[0]; x++)
                line[x] = xyzarray_get(xyz, (ivec3){ x, y, z }) ? '#' : ' ';
            dbg(" #%d# |%s|\n", z, line);
        }
    free(line);
}

int xyzarray_count(struct xyzarray *xyz)
{
    int x, y, z, count = 0;

    for (z = 0; z < xyz->dim[2]; z++)
        for (y = 0; y < xyz->dim[1]; y++)
            for (x = 0; x < xyz->dim[0]; x++)
                if (xyzarray_get(xyz, (ivec3){ x, y, z}))
                    count++;
    return count;
}

unsigned char *xyarray_new(int width)
{
    struct xyzarray *xyz = xyzarray_new((ivec3){ width, width, 1 });

    if (!xyz)
        return NULL;

    return &xyz->arr[0];
}

void xyarray_free(unsigned char *arr)
{
    struct xyzarray *xyz = container_of(arr, struct xyzarray, arr);

    free(xyz);
}

unsigned char xyarray_get(unsigned char *arr, int x, int y)
{
    struct xyzarray *xyz = container_of(arr, struct xyzarray, arr);

    return xyzarray_get(xyz, (ivec3){ x, y, 0 });
}

void xyarray_set(unsigned char *arr, int x, int y, unsigned char v)
{
    struct xyzarray *xyz = container_of(arr, struct xyzarray, arr);

    xyzarray_set(xyz, (ivec3){ x, y, 0 }, v);
}

void xyarray_print(unsigned char *arr)
{
    struct xyzarray *xyz = container_of(arr, struct xyzarray, arr);
    static const char ch[] = " .+oO############_^tTF";
    int height = xyz->dim[1];
    int width = xyz->dim[0];
    char str[2048];
    int i, j, p;

    for (j = 0; j < height; j++) {
        for (i = 0, p = 0; i < width; i++)
            p += snprintf(str + p, sizeof(str) - p, "%c ",
                          ch[xyarray_get(arr, i, j)]);
        dbg("arr[%02d]: %s\n", j, str);
    }
}
