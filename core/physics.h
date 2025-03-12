/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PHYSICS_H__
#define __CLAP_PHYSICS_H__

#ifdef __EMSCRIPTEN__
#define dSINGLE
#else
#define dDOUBLE
#endif
#include "linmath.h"

/*
 * Type of struct phys_body:
 * * PHYS_GEOM: just a geometry, doesn't directly participate in dynamics
 *              simulation, only via collisions
 * * PHYS_BODY: has a dBodyID in addition to dGeomID, moves around and
 *              callides with other bodies and geometries
 */
typedef enum phys_type {
    PHYS_BODY = 0,
    PHYS_GEOM,
} phys_type;

/*
 * Geometry classes: these directly map onto ODE's classes, at least for
 * now. In practice, we only use sphere, capsule and trimesh.
 */
typedef enum geom_class {
    GEOM_SPHERE = 0,
    GEOM_BOX,
    GEOM_CAPSULE,
    GEOM_CYLINDER,
    GEOM_PLANE,
    GEOM_RAY,
    GEOM_CONVEX,
    GEOM_TRANSFORM,
    GEOM_TRIMESH,
    GEOM_HEIGHTFIELD,
    GEOM_SIMPLE_SPACE,
    GEOM_HASH_SPACE,
    GEOM_SWEEP_AND_PRUNE_SPACE,
    GEOM_TREE_SPACE
} geom_class;

struct phys_body;

typedef void (*ground_contact_fn)(void *priv, float x, float y, float z);

struct phys;
typedef struct entity3d entity3d;

typedef struct phys_contact_params {
    float   bounce;
    float   bounce_vel;
} phys_contact_params;

/* Set body's contact parameters */
#define phys_body_set_contact_params(_b, args...) \
    _phys_body_set_contact_params((_b), &(phys_contact_params){ args })
void _phys_body_set_contact_params(struct phys_body *body, const phys_contact_params *params);

void phys_step(struct phys *phys, double dt);
struct phys *phys_init(void);
void phys_done(struct phys *phys);
/* Set the global ground_contact callback */
void phys_set_ground_contact(struct phys *phys, ground_contact_fn ground_contact);
void phys_ground_add(entity3d *e);
/*
 * Find the nearest intersecting geometry with the ray at @start in the
 * direction @dir and length *pdist. If there is an intersection, the distance
 * will be written to *pdist. Entity @e must have a phys_body. If it doesn't,
 * use phys_ray_cast2(), which instead requires a struct phys pointer.
 */
entity3d *phys_ray_cast(entity3d *e, vec3 start, vec3 dir, double *pdist);
/*
 * Same as phys_ray_cast() for the entities that don't have a phys_body, so it
 * requires a pointer to the physics state (struct phys).
 */
entity3d *phys_ray_cast2(struct phys *phys, entity3d *e, vec3 start, vec3 dir,
                         double *pdist);
/* Check if phys_body has a body (true) or just a geometry (false) */
bool phys_body_has_body(struct phys_body *body);
entity3d *phys_body_entity(struct phys_body *body);
/* Get phys_body's position */
void phys_body_position(struct phys_body *body, vec3 pos);
/* Get phys_body's rotation quaternion */
void phys_body_rotation(struct phys_body *body, quat rot);
/* Create a new phys_body for entity */
struct phys_body *phys_body_new(struct phys *phys, entity3d *entity, geom_class class,
                                double geom_radius, double geom_offset, phys_type type,
                                double mass);
int phys_body_update(entity3d *e);
void phys_body_done(struct phys_body *body);
void phys_body_attach_motor(struct phys_body *body, bool attach);
/* Set body's body or geometry's position, taking into account body::yoffset */
void phys_body_set_position(struct phys_body *body, vec3 pos);
/* Enable/disable a body */
void phys_body_enable(struct phys_body *body, bool enable);
/* Get the physical body's linear velocity */
void phys_body_get_velocity(struct phys_body *body, vec3 vel);
/* Set the physical body's linear velocity */
void phys_body_set_velocity(struct phys_body *body, vec3 vel);
void phys_body_set_motor_velocity(struct phys_body *body, bool body_also, vec3 vel);
void phys_body_stop(struct phys_body *body);
bool phys_body_ground_collide(struct phys_body *body, bool grounded);
void phys_ground_entity(struct phys *phys, entity3d *e);

struct scene;
void phys_debug_draw(struct scene *scene, struct phys_body *body);

#endif /* __CLAP_PHYSICS_H__ */
