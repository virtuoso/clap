/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PHYSICS_COMMON_H__
#define __CLAP_PHYSICS_COMMON_H__

/*
 * For the physics backends only: what they work out the same way whatever
 * the library underneath (physics-common.c); a backend only hands the
 * results to its library
 */

#include "physics.h"

/**
 * struct phys_capsule - an entity's capsule: its size and where it sits
 * @radius:     the caps' radius
 * @length:     between the caps' centers; 0 makes it a sphere
 * @yoffset:    its center above the entity's origin
 * @ray_off:    from its center down to where the ground ray starts
 * @axis:       the entity's longest axis, which the sizing follows: 0 for
 *              X, 1 for Y, 2 for Z
 */
typedef struct phys_capsule {
    float   radius;
    float   length;
    float   yoffset;
    float   ray_off;
    int     axis;
} phys_capsule;

/**
 * phys_capsule_size() - size an entity's capsule
 * @e:      entity3d
 * @radius: the caps' radius; 0 sizes it from the entity's aabb
 * @offset: its center above the entity's origin; 0 from the aabb
 *
 * The capsule of phys_body_new(), whose geom_* parameters these are, at the
 * entity's scale.
 * Return: the capsule
 */
phys_capsule phys_capsule_size(entity3d *e, double radius, double offset);

#endif /* __CLAP_PHYSICS_COMMON_H__ */
