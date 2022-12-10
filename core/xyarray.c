// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include "logger.h"

unsigned char xyarray_get(unsigned char *arr, int width, int x, int y)
{
    if (x < 0 || y < 0 || x >= width || y >= width)
        return 0;
    if (x < 0)
        x = width - 1;
    else if (x >= width)
        x = 0;
    return arr[y * width + x];
}

void xyarray_set(unsigned char *arr, int width, int x, int y, unsigned char v)
{
    if (x < 0)
        x = width - 1;
    else if (x >= width)
        x = 0;
    arr[y * width + x] = v;
}

void xyarray_print(unsigned char *arr, int width, int height)
{
    static const char ch[] = " .+oO############_^tTF";
    char str[2048];
    int i, j, p;

    for (j = 0; j < height; j++) {
        for (i = 0, p = 0; i < width; i++)
            // p += sprintf(str + p, "%.01x ", xyarray_get(arr, width, i, j));
            p += sprintf(str + p, "%c ", ch[xyarray_get(arr, width, i, j)]);
        dbg("arr[%02d]: %s\n", j, str);
    }
}
