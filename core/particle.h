/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PARTICLE_H__
#define __CLAP_PARTICLE_H__

#include "linmath.h"
#include "object.h"
#include "render.h"

typedef struct particle_system particle_system;
typedef struct particle particle;
typedef struct entity3d entity3d;

typedef enum particle_dist {
    PART_DIST_LIN = 0,
    PART_DIST_SQRT,
    PART_DIST_CBRT,
    PART_DIST_POW075,
} particle_dist;

DEFINE_REFCLASS_INIT_OPTIONS(particle_system,
    const char          *name;
    struct shader_prog  *prog;
    struct mq           *mq;
    vec3                center;
    double              radius;
    double              min_radius;
    double              scale;
    double              velocity;
    unsigned int        count;
    particle_dist       dist;
    texture_t           *tex;
    texture_t           *emit;
    float               bloom_intensity;
);
DECLARE_REFCLASS(particle_system);

// void particle_system_position(particle_system *ps, vec3 center, float rx, float ry, float rz);
entity3d *particle_system_entity(particle_system *ps);
void particle_system_position(particle_system *ps, const vec3 center);
void particle_system_upload(particle_system *ps, struct shader_prog *prog);
unsigned int particle_system_count(particle_system *ps);

#endif /* __CLAP_PARTICLE_H__ */
