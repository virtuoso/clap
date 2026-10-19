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
 * @length: between the caps' centers; 0 from the aabb
 *
 * The capsule of phys_body_new(), whose geom_* parameters these are, at the
 * entity's scale.
 * Return: the capsule
 */
phys_capsule phys_capsule_size(entity3d *e, double radius, double offset, double length);

/**
 * phys_limb_joint_valid() - check that a limb can ride a joint
 * @e:      entity3d
 * @joint:  joint index
 *
 * Return: true if @e has a skeleton and @joint is one of its joints
 */
bool phys_limb_joint_valid(entity3d *e, int joint);

/**
 * phys_limb_vx() - a piece's vertices in the frame of the limb riding a joint
 * @e:      entity3d with an armature
 * @joint:  the joint the limb rides
 * @vx:     the piece's vertices in the model at the bind pose
 * @nr_vx:  how many
 *
 * The limb's body rides the joint, so its shape is in the joint's frame;
 * like the other limb dimensions, before the entity's scale.
 * Return: a new array of @nr_vx vertices
 */
vec3 *phys_limb_vx(entity3d *e, int joint, const vec3 *vx, size_t nr_vx);

/**
 * phys_limb_place() - where a limb is this frame
 * @e:      entity3d with an armature
 * @joint:  the joint the limb rides
 * @offset: the limb's center in the joint's frame
 * @pos:    [out] the limb's center in the world
 * @rot:    [out] its orientation in the world: the joint's
 *
 * entity3d::mx * joint_transforms[@joint] * bind * @offset: where the
 * latest pose put the joint.
 */
void phys_limb_place(entity3d *e, int joint, const vec3 offset, vec3 pos, mat4x4 rot);

/**
 * struct phys_pen_walk - a phys_body_for_each_pen() under way
 * @params: its parameters
 * @count:  how many entities went to phys_pen_params::fn so far
 *
 * The backend finds what reaches into the body, and hands each entity to
 * phys_pen_walk_visit() once.
 */
typedef struct phys_pen_walk {
    const phys_pen_params   *params;
    size_t                  count;
} phys_pen_walk;

/**
 * phys_pen_walk_init() - start a phys_body_for_each_pen()
 * @walk:   [out] the walk
 * @params: phys_body_for_each_pen()'s parameters
 *
 * Return: CERR_OK, or CERR_INVALID_ARGUMENTS without phys_pen_params::fn or
 *         with a negative depth
 */
cerr phys_pen_walk_init(phys_pen_walk *walk, const phys_pen_params *params);

/**
 * phys_pen_walk_visit() - hand an entity that reaches into the body to the callback
 * @walk:   the walk
 * @e:      the entity
 *
 * Return: true to go on, false if the callback asks to stop
 */
bool phys_pen_walk_visit(phys_pen_walk *walk, entity3d *e);

/**
 * phys_pen_walk_done() - finish a phys_body_for_each_pen()
 * @walk:   the walk
 *
 * Return: phys_body_for_each_pen()'s result: how many entities went to the
 *         callback, or CERR_NOT_FOUND if none
 */
cres(size_t) phys_pen_walk_done(const phys_pen_walk *walk);

#endif /* __CLAP_PHYSICS_COMMON_H__ */
