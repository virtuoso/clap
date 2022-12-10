// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stddef.h>
#include <time.h>
#define dbg printf
#define ui_debug_printf(...)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min3(a, b, c) (min(a, min(b, c)))
#define array_size(x) (sizeof(x) / sizeof(*x))
#define __stringify(x) (# x)
#define CHECK(x) x
#define CUBE_SIDE 8
#define __CLAP_LOGGER_H__
#define __CLAP_UI_DEBUG_H__

#include "../core/ca3d.c"

int main()
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand48(ts.tv_nsec);

    struct xyzarray *xyz = ca3d_make(16, 8, 4);
    xyzarray_print(xyz);
    ca3d_run(xyz, ca_coral, 4);
    xyzarray_print(xyz);
    printf("total cubes: %d\n", xyzarray_count(xyz));
    return 0;
}
