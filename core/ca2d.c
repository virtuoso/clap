// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include "ca2d.h"
#include "util.h"
#include "xyarray.h"

/*
 * 2D cellular automata
 */

int ca2d_neigh_vn1(unsigned char *arr, int x, int y)
{
    int n = 0;

    n += !!xyarray_get(arr, x + 1, y);
    n += !!xyarray_get(arr, x - 1, y);
    n += !!xyarray_get(arr, x, y + 1);
    n += !!xyarray_get(arr, x, y - 1);

    return n;
}

int ca2d_neigh_m1(unsigned char *arr, int x, int y)
{
    int n = ca2d_neigh_vn1(arr, x, y);

    n += !!xyarray_get(arr, x + 1, y + 1);
    n += !!xyarray_get(arr, x - 1, y + 1);
    n += !!xyarray_get(arr, x + 1, y - 1);
    n += !!xyarray_get(arr, x - 1, y - 1);

    return n;
}

int ca2d_neigh_vnv(unsigned char *arr, int x, int y)
{
    int n = 0;
    int v = xyarray_get(arr, x, y);

    n += xyarray_get(arr, x + 1, y) > v;
    n += xyarray_get(arr, x - 1, y) > v;
    n += xyarray_get(arr, x, y + 1) > v;
    n += xyarray_get(arr, x, y - 1) > v;

    return n;
}

int ca2d_neigh_mv(unsigned char *arr, int x, int y)
{
    int n = ca2d_neigh_vnv(arr, x, y);
    int v = xyarray_get(arr, x, y);

    n += xyarray_get(arr, x + 1, y + 1) > v;
    n += xyarray_get(arr, x - 1, y + 1) > v;
    n += xyarray_get(arr, x + 1, y - 1) > v;
    n += xyarray_get(arr, x - 1, y - 1) > v;

    return n;
}

void ca2d_step(const struct cell_automaton *ca, unsigned char *arr, int side)
{
    int i, j;

    for (i = 0; i < side; i++)
        for (j = 0; j < side; j++) {
            int n = ca->neigh_2d(arr, i, j);
            int v = xyarray_get(arr, i, j);

            if (!v && (ca->born_mask & (1 << n)))
                xyarray_set(arr, i, j, ca->nr_states);
            else if (v && (ca->surv_mask & (1 << n)))
                ;
            else if (v && ca->decay)
                xyarray_set(arr, i, j, v - 1);
        }
}

unsigned char *ca2d_generate(const struct cell_automaton *ca, int side, int steps)
{
    unsigned char *arr;
    int i, j, step;

    CHECK(arr = xyarray_new(side));
    /* seed */
    for (i = 0; i < side; i++)
        for (j = 0; j < side; j++) {
            int v = lrand48() % 8;
            xyarray_set(arr, i, j, v <= ca->nr_states ? ca->nr_states : 0);
        }

    for (step = 0; step < steps; step++) {
        ca2d_step(ca, arr, side);
    }
    // xyarray_print(arr, side, side);

    return arr;
}
