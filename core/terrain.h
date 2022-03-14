/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_TERRAIN_H__
#define __CLAP_TERRAIN_H__

#include "object.h"

struct terrain {
    struct ref     ref;
    struct entity3d *entity;
    long           seed;
    float *vx, *norm, *tx;
    unsigned short *idx;
    size_t         nr_vx;
    size_t         nr_idx;

    float          *map, *map0;
    float x, y, z;
    unsigned int   side;
    unsigned int   nr_vert;
};

static inline size_t terrain_vxsz(struct terrain *t)
{
    return t->nr_vx * sizeof(*t->vx) * 3;
}

static inline size_t terrain_txsz(struct terrain *t)
{
    return t->nr_vx * sizeof(*t->vx) * 2;
}

static inline size_t terrain_idxsz(struct terrain *t)
{
    return t->nr_idx * sizeof(*t->idx);
}

float terrain_height(struct terrain *t, float x, float z);
void terrain_normal(struct terrain *t, float x, float z, vec3 n);
struct terrain *terrain_init(struct scene *s, float x, float y, float z, float side, unsigned int nr_v);
struct terrain *terrain_init_square_landscape(struct scene *s, float x, float y, float z, float side, unsigned int nr_v);
struct terrain *terrain_init_circular_maze(struct scene *s, float x, float y, float z, float side, unsigned int nr_v, unsigned int nr_levels);
void terrain_done(struct terrain *terrain);

#endif /* __CLAP_TERRAIN_H__ */
