/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PHYSICS_H__
#define __CLAP_PHYSICS_H__

#ifdef __EMSCRIPTEN__
#define dSINGLE
#else
#define dDOUBLE
#endif
#include "linmath.h"

/**
 * enum phys_type - physics body's type
 * Type of struct phys_body:
 * @PHYS_GEOM:  just a geometry, doesn't directly participate in dynamics
 *              simulation, only via collisions
 * @PHYS_BODY:  has a dBodyID in addition to dGeomID, moves around and
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

/**
 * struct phys_contact_params - geom-geom contact parameters
 * @bounce:     [ODE] "contact resolution" by bouncing [0.0, 1.0]: 0.0: not
 *              bouncy at all, 1.0: maximum bounciness
 * @bounce_vel: minimal incoming velocity necessary to bounce, below this bounce
 *              is zero
 */
typedef struct phys_contact_params {
    float   bounce;
    float   bounce_vel;
} phys_contact_params;

/**
 * define phys_body_set_contact_params - set body's contact parameters
 * @_b:     phys_body object
 * @args:   comma-separated list of contact parameters' initializers
 *          (see &struct phys_contact_params)
 *
 * This is syntax sugar for _phys_body_set_contact_params() that requires
 * less typing and only specifying relevant parameters.
 */
#define phys_body_set_contact_params(_b, args...) \
    _phys_body_set_contact_params((_b), &(phys_contact_params){ args })

/**
 * _phys_body_set_contact_params() - set body's contact parameters
 * @body:   phys_body object
 * @params: contact parameters (see &struct phys_contact_params)
 *
 * Set @body's contact parameters. Instead of this, use
 * phys_body_set_contact_params() convenience macro.
 */
void _phys_body_set_contact_params(struct phys_body *body, const phys_contact_params *params);

/**
 * phys_step() - perform physics simulation for a given time frame
 * @phys:   core physics handle
 * @dt:     time delta for which to perform the simulation
 *
 * This does 2 things: processes collisions between geometries and performs
 * a dynamics simulation. @dt is divided into constant time slices and for
 * each of them, a "step" is taken.
 */
void phys_step(struct phys *phys, double dt);

/**
 * phys_init() - initialize physics
 *
 * Initialize global physics state for clap.
 * Return: global physics handle object
 */
struct phys *phys_init(void);

/**
 * phys_done() - deinitialize physics
 * @phys:   core physics handle
 *
 * Clear spaces, undo phys_init(), deallocate @phys.
 */
void phys_done(struct phys *phys);

/**
 * phys_contacts_debug_enable() - enable debug draw of contact points
 * @phys:   core physics handle
 * @enable: enable/disable debug draws
 *
 * Enable/disable debug drawing of contact points from collision detection.
 */
void phys_contacts_debug_enable(struct phys *phys, bool enable);

/**
 * phys_capsules_debug_enable() - enable debug draw of character capsules
 * @phys:   core physics handle
 * @enable: enable/disable debug draws
 *
 * Enable/disable debug drawing of characters' capsules.
 */
void phys_capsules_debug_enable(struct phys *phys, bool enable);

/* XXX: remove these 2 */
void phys_set_ground_contact(struct phys *phys, ground_contact_fn ground_contact);
void phys_ground_add(entity3d *e);

/**
 * phys_ray_cast() - cast a ray from
 * @e:      entity3d object with a phys_body from which to cast the ray
 * @start:  ray's starting point
 * @dir:    ray's direction
 * @pdist:  [in] ray length, [out] distance to contact
 *
 * Find the nearest intersecting geometry with the ray at @start in the
 * direction @dir and length *pdist. If there is an intersection, the distance
 * will be written to *pdist. Entity @e must have a phys_body. If it doesn't,
 * use phys_ray_cast2(), which instead requires a struct phys pointer.
 * Return: entity3d of the intersecting body or NULL
 */
entity3d *phys_ray_cast(entity3d *e, const vec3 start, const vec3 dir, double *pdist);

/**
 * phys_ray_cast2() - cast a ray
 * @phys:   core physics handle
 * @e:      entity3d object without phys_body from which to cast the ray
 * @start:  ray's starting point
 * @dir:    ray's direction
 * @pdist:  [in] ray length, [out] distance to contact
 *
 * Same as phys_ray_cast() for the entities that don't have a phys_body, so it
 * requires a pointer to the physics state @phys (&struct phys).
 * Return: entity3d of the intersecting body or NULL
 */
entity3d *phys_ray_cast2(struct phys *phys, entity3d *e, const vec3 start, const vec3 dir,
                         double *pdist);

/**
 * phys_body_has_body() - check if phys_body is a body or a geometry
 * @body:   phys_body to test
 *
 * Return: true if phys_body is a body, false: geom without a body
 */
bool phys_body_has_body(struct phys_body *body);

/**
 * phys_body_entity() - get phys_body's entity3d
 * @body:   phys_body to query
 *
 * Return: entity3d object that owns this phys_body
 */
entity3d *phys_body_entity(struct phys_body *body);

/**
 * phys_body_position() - get phys_body's position
 * @body:   phys_body to query
 * @pos:    receiver of the body's coordinates
 */
void phys_body_position(struct phys_body *body, vec3 pos);

/**
 * phys_body_rotation() - get phys_body's rotation quaternion
 * @body:   phys_body to query
 * @rot:    receiver of the body's rotation quaternion
 */
void phys_body_rotation(struct phys_body *body, quat rot);

/**
 * phys_body_new() - create a new phys_body for entity
 * @phys:           core physics handle
 * @entity:         entity3d object for which to create a phys_body
 * @class:          geometry class (see &enum geom_class)
 * @geom_radius:    radius (for sphere and capsule)
 * @geom_offset:    vertical offset from entity3d's position (for sphere and capsule)
 * @type:           PHYS_BODY or PHYS_GEOM (see &enum phys_type)
 * @mass:           mass (for PHYS_BODY)
 *
 * Create a phys_body for a given @entity with specified parameters. Some
 * parameters are only valid for certain @class / @type.
 * Return: phys_body object or NULL if something goes wrong
 */
struct phys_body *phys_body_new(struct phys *phys, entity3d *entity, geom_class class,
                                double geom_radius, double geom_offset, phys_type type,
                                double mass);

/**
 * phys_body_update() - update entity's position from its phys_body
 * @e:  entity3d object
 *
 * Get the phys_body position and translate it into @e's position (taking
 * into account body::yoffset). Then, check the body's velocity and return 1
 * if it's in motion.
 * TODO: this API is ripe for refactoring.
 * Return: 1 if the body has a non-zero linear velocity, 0 otherwise
 */
int phys_body_update(entity3d *e);

/**
 * phys_body_done() - destroy a phys_body
 * @body:   phys_body object
 *
 * Destroy a phys_body and everything it owns.
 */
void phys_body_done(struct phys_body *body);

/**
 * phys_body_attach_motor() - attach phys_body to its linear motor
 * @body:   phys_body object
 * @attach: true to attach, false to detach
 *
 * Attach/detach a phys_body to/from its linear motor that facilitates, for example,
 * character movements. Typically, it a body is airborne (jumping or falling), it's
 * detached from the motor, and reattaches upon landing.
 */
void phys_body_attach_motor(struct phys_body *body, bool attach);

/**
 * phys_body_set_position() - set body's body or geometry's position
 * @body:   phys_body object
 * @pos:    new position
 *
 * Set body's body or geometry's position, taking into account body::yoffset
 */
void phys_body_set_position(struct phys_body *body, const vec3 pos);

/**
 * phys_body_rotate_mat4x4() - set body's rotation from 4x4 TRS matrix
 * @body:   phys_body object
 * @trs:    TRS matrix, from which the rotation is extracted
 *
 * Set body's rotation from @trs. Rotation itself in the 3x3 matrix, so
 * extract it from the 4x4 @trs and apply it to phys_body's body or geometry.
 */
void phys_body_rotate_mat4x4(struct phys_body *body, mat4x4 trs);

/**
 * phys_body_enable() - enable/disable a body
 * @body:   phys_body object
 * @enable: true to enable, false to disable
 *
 * Enable/disable phys_body's body. Needs to be PHYS_BODY.
 */

void phys_body_enable(struct phys_body *body, bool enable);

/**
 * phys_body_get_velocity() - get the physical body's linear velocity
 * @body:   phys_body object
 * @vel:    receiver for the velocity vector
 *
 * Retrieve phys_body's body's linear velocity and write it to @vel.
 * Needs to by a PHYS_BODY.
 */
void phys_body_get_velocity(struct phys_body *body, vec3 vel);

/**
 * phys_body_set_velocity() - set the physical body's linear velocity
 * @body:   phys_body object
 * @vel:    linear velocity vector
 *
 * Set phys_body's body's linear velocity from @vel.
 * Needs to by a PHYS_BODY.
 */
void phys_body_set_velocity(struct phys_body *body, vec3 vel);

/**
 * phys_body_set_motor_velocity() - set phys_body's linear motor velocity
 * @body:       phys_body object
 * @body_also:  apply velocity directly to the boby as well
 * @vel:        linear velocity vector
 *
 * Set the @body's linear motor's velocity to @vel. If the body is not attached
 * to the motor, attach it first. If @body_also is true, also set the body's
 * linear velocity directly.
 */
void phys_body_set_motor_velocity(struct phys_body *body, bool body_also, vec3 vel);

/**
 * phys_body_stop() - stop the physical body
 * @body:   phys_body object
 *
 * Clear the @body's linear motor velocity.
 */
void phys_body_stop(struct phys_body *body);

/**
 * phys_body_ground_collide() - test if body is on/in the ground
 * @body:       phys_body object
 * @grounded:   true if the body was grounded before this call
 *
 * TODO: This does a whole lot of things and is ripe for refactoring.
 *
 * TODO: This is also the source of the "slow falling" when in contact
 * with the ground.
 *
 * The "ground" is anything in the phys::ground_space, which are collision
 * geometries and, strictly speaking, not necessarily "ground" in the literal
 * sense of the word.
 *
 * @body in this case is really a character, which is a capsule suspended at
 * about knee height that casts downwards rays to find its position relative
 * to the ground.
 *
 * What this function does:
 * * check if the body's @body (dBody) directly collides with the ground
 *   space, in which case it tests the angle of contact and if it's "vertical
 *   enough", adjusts the vertical position of the body by @body::yoffset aka
 *   "legs are in the ground", which shouldn't normally happen as later on
 *   this function performs height correction if the body is slightly sinking
 *   into the ground; if the angle of contact is "less vertical", it's considered
 *   running into an obstacle; in both cases the body is stopped
 * * cast downward rays to find the ground and, depending on the distance to
 *   the intersection, performs height adjustments one way or the other or
 *   leaves it be.
 * Return: true if @body is in contact with the ground, false otherwise.
 */
bool phys_body_ground_collide(struct phys_body *body, bool grounded);

/**
 * phys_ground_entity() - force the entity3d to the ground
 * @phys:   core physics handle
 * @e:      entity3d object
 *
 * Whatever ground entity is directly below @e, place @e on top of it aka
 * "ground" the entity @e.
 */
void phys_ground_entity(struct phys *phys, entity3d *e);

struct scene;
/**
 * phys_debug_draw() - draw phys_body using debug_draw
 * @scene:  scene object
 * @body:   phys_body object
 *
 * If @body is a capsule or a sphere and capsule draw is enabled, use debug
 * draw facilities to drow it.
 */
void phys_debug_draw(struct scene *scene, struct phys_body *body);

#endif /* __CLAP_PHYSICS_H__ */
