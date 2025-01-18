// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include "logger.h"
#include "ca3d.h"
#include "xyarray.h"

/*
 * 3D cellular automata
 */

/* Von Neumann, Manhattan distance 1 */
int ca3d_neighbors_vn1(struct xyzarray *xyz, int x, int y, int z)
{
    int neigh = 0;

    neigh += !!xyzarray_get(xyz, (ivec3){ x - 1, y, z });
    neigh += !!xyzarray_get(xyz, (ivec3){ x + 1, y, z });
    neigh += !!xyzarray_get(xyz, (ivec3){ x, y - 1, z });
    neigh += !!xyzarray_get(xyz, (ivec3){ x, y + 1, z });
    neigh += !!xyzarray_get(xyz, (ivec3){ x, y, z - 1 });
    neigh += !!xyzarray_get(xyz, (ivec3){ x, y, z + 1 });

    return neigh;
}

static int ca3d_neighbors_m1(struct xyzarray *xyz, int x, int y, int z)
{
    int neigh = 0, cx, cy, cz;

    for (cz = z - 1; cz < z + 2; cz++)
        for (cy = y - 1; cy < y + 2; cy++)
            for (cx = x - 1; cx < x + 2; cx++)
                neigh += !!xyzarray_get(xyz, (ivec3){ cx, cy, cz });
    neigh -= !!xyzarray_get(xyz, (ivec3){ x, y, z });
    return neigh;
}

int ca3d_prune(struct xyzarray *xyz)
{
    int x, y, z, cnt = 0;

    for (z = 0; z < xyz->dim[2]; z++)
        for (y = 0; y < xyz->dim[1]; y++)
            for (x = 0; x < xyz->dim[0]; x++)
                if (ca3d_neighbors_vn1(xyz, x, y, z) == 6)
                    xyzarray_set(xyz, (ivec3){ x, y, z}, -1);
    for (z = 0; z < xyz->dim[2]; z++)
        for (y = 0; y < xyz->dim[1]; y++)
            for (x = 0; x < xyz->dim[0]; x++)
                if (xyzarray_get(xyz, (ivec3){ x, y, z }) == -1) {
                    xyzarray_set(xyz, (ivec3){ x, y, z }, 0);
                    cnt++;
                }

    return cnt;
}

#define HIST_SIZE 128
#define TRIES 12
static int ca3d_walk(struct xyzarray *xyz, int steps, int val)
{
    int try, step, dir, histp = 0;
    ivec3 history[HIST_SIZE];
    ivec3 cur;

    cur[0] = xyz->dim[0] / 2;
    cur[1] = xyz->dim[1] / 2;
    cur[2] = xyz->dim[2] / 2;

    for (step = 0; step < steps; step++) {
        ivec3 next;

        // dbg(" => at [%d, %d, %d]\n", cur[0], cur[1], cur[2]);
        xyzarray_set(xyz, cur, val);
        for (try = 0; try < TRIES; try++) {
            memcpy(next, cur, sizeof(cur));
            dir = lrand48() % 3;
            next[dir] += lrand48() & 1 ? 1 : -1;
            if (xyzarray_valid(xyz, next) && !xyzarray_get(xyz, next))
                goto got_it;
        }

        /* exceeded TRIES: roll back */
        memcpy(cur, history[--histp], sizeof(cur));
        // dbg(" <= rolling back to [%d, %d, %d]\n", cur[0], cur[1], cur[2]);
        continue;
got_it:
        if (histp == HIST_SIZE)
            continue;
        memcpy(history[histp++], next, sizeof(next));
        memcpy(cur, next, sizeof(next));
    }

    ca3d_prune(xyz);
    return xyzarray_count(xyz);
}

#define CA_DEF(_name, _surv, _born, _nrst, _neigh) \
    [_name] = { \
        .surv_mask = (_surv), \
        .born_mask = (_born), \
        .nr_states = (_nrst), \
        .neigh_3d  = (ca3d_neighbors_ ## _neigh), \
        .name      = __stringify(_name), \
    }

static struct cell_automaton cas[] = {
    CA_DEF(ca_445m, CA_4, CA_4, 5, m1),
    CA_DEF(ca_678_678_3m, CA_6|CA_7|CA_8, CA_6|CA_7|CA_8, 3, m1),
    CA_DEF(ca_pyroclastic, CA_4|CA_5|CA_6|CA_7, CA_6|CA_7|CA_8, 10, m1),
    CA_DEF(ca_amoeba, CA_RANGE(9, 26), CA_5|CA_6|CA_7|CA_12|CA_13|CA_15, 5, m1),
    CA_DEF(ca_builder, CA_2|CA_6|CA_9, CA_4|CA_6|CA_8|CA_9, 10, m1),
    CA_DEF(ca_slow_decay, CA_1|CA_4|CA_8|CA_11|CA_RANGE(13, 26), CA_RANGE(13, 26), 5, m1),
    CA_DEF(ca_spiky_growth,
           CA_RANGE(0, 3)|CA_RANGE(7, 9)|CA_RANGE(11, 13)|CA_18|CA_21|CA_22|CA_24|CA_26,
           CA_4|CA_13|CA_17|CA_RANGE(20, 24)|CA_26, 4, m1),
    CA_DEF(ca_coral, CA_RANGE(5, 8), CA_RANGE(6, 7)|CA_9|CA_12, 4, m1),
    CA_DEF(ca_crystal_1, CA_RANGE(0, 6),CA_1|CA_3, 2, vn1),
};

int ca3d_run(struct xyzarray *xyz, int nca, int steps)
{
    struct cell_automaton *ca = &cas[nca % array_size(cas)];
    int x, y, z, neigh, state;

    for (; steps; steps--)
        for (z = 0; z < xyz->dim[2]; z++)
            for (y = 0; y < xyz->dim[1]; y++)
                for (x = 0; x < xyz->dim[0]; x++) {
                    neigh = ca3d_neighbors_m1(xyz, x, y, z);
                    state = xyzarray_get(xyz, (ivec3){ x, y, z });

                    if (state && !(ca->surv_mask & (1 << neigh)))
                        xyzarray_set(xyz, (ivec3) { x, y, z }, state - 1);
                    else if (!state && (ca->born_mask & (1 << neigh)))
                        xyzarray_set(xyz, (ivec3) { x, y, z }, ca->nr_states - 1);
                }
    return xyzarray_count(xyz);
}

struct xyzarray *ca3d_make(int d0, int d1, int d2)
{
    ivec3 dim = { d0, d1, d2 };
    struct xyzarray *xyz = xyzarray_new(dim);
    int steps = min3(d0 * d1, d1 * d2, d0 * d2);
    int x, y, z;

    for (x = 0; x < d0; x++)
        for (y = 0; y < d1; y++) {
            xyzarray_set(xyz, (ivec3){ x, y, 0 }, 5 );
            xyzarray_set(xyz, (ivec3){ x, y, d2 - 1 }, 5);
        }
    for (x = 0; x < d0; x++)
        for (z = 0; z < d2; z++) {
            xyzarray_set(xyz, (ivec3){ x, 0, z }, 5);
            xyzarray_set(xyz, (ivec3){ x, d1 - 1, z}, 5);
        }
    for (y = 0; y < d1; y++)
        for (z = 0; z < d2; z++) {
            xyzarray_set(xyz, (ivec3){ 0, y, z }, 5);
            xyzarray_set(xyz, (ivec3){ d0 - 1, y, z }, 5);
        }
    ca3d_walk(xyz, steps, 5);

    return xyz;
}
