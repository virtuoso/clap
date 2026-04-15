/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PHYSICS_H__
#define __CLAP_PHYSICS_H__

#ifdef __EMSCRIPTEN__
#define dSINGLE
#else
#define dDOUBLE
#endif
#include "linmath.h"
#include "transform.h"

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

struct phys;
typedef struct entity3d entity3d;

/**
 * struct phys_contact_params - geom-geom contact parameters
 * @bounce:     [ODE] "contact resolution" by bouncing [0.0, 1.0]: 0.0: not
 *              bouncy at all, 1.0: maximum bounciness
 * @bounce_vel: minimal incoming velocity necessary to bounce, below this bounce
 *              is zero
 * @mu:         Coulomb friction coefficient [0.0, dInfinity]: 0.0: frictionless,
 *              dInfinity: never slides
 * @soft_erp:   per-body error reduction parameter override [0.0, 1.0]:
 *              0 means use the default; lower values reduce penetration
 *              correction force (helps prevent tall objects from bouncing
 *              back upright)
 * @soft_cfm:   per-body constraint force mixing override:
 *              0 means use the default; higher values make contacts softer
 */
typedef struct phys_contact_params {
    float   bounce;
    float   bounce_vel;
    float   mu;
    float   soft_erp;
    float   soft_cfm;
} phys_contact_params;

/**
 * phys_body_set_contact_params - set body's contact parameters
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
 * _phys_body_set_contact_params - set body's contact parameters
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

struct clap_context;

/**
 * phys_init() - initialize physics
 * @ctx:    clap context
 *
 * Initialize global physics state for clap.
 * Return: global physics handle object
 */
struct phys *phys_init(struct clap_context *ctx);

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

/**
 * phys_velocities_debug_enable() - enable debug draw of body's velocity
 * @phys:   core physics handle
 * @enable: enable/disable debug draws
 *
 * Enable/disable debug drawing of a body's velocity.
 */
void phys_velocities_debug_enable(struct phys *phys, bool enable);

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
 * phys_body_set_position() - set body's body or geometry's position
 * @body:   phys_body object
 * @pos:    new position
 *
 * Set body's body or geometry's position, taking into account body::yoffset
 */
void phys_body_set_position(struct phys_body *body, const vec3 pos);

/**
 * phys_body_move() - move body by a delta vector
 * @body:   phys_body object
 * @delta:  movement vector to add to current position
 *
 * Move phys_body's body or geometry by @delta relative to its current
 * position. Unlike phys_body_set_position(), this bypasses the update
 * guard and always applies the movement.
 */
void phys_body_move(struct phys_body *body, const vec3 delta);

/**
 * phys_body_rotate_mat3x3() - set body's rotation from 3x3 rotation matrix
 * @body:   phys_body object
 * @rot:    3x3 rotation matrix
 *
 * Apply @rot to phys_body's body or geometry.
 */
void phys_body_rotate_mat3x3(struct phys_body *body, mat3x3 rot);

/**
 * phys_body_rotate_mat4x4() - set body's rotation from 4x4 TRS matrix
 * @body:   phys_body object
 * @trs:    TRS matrix, from which the rotation is extracted
 *
 * Extract the 3x3 rotation from @trs and apply it to phys_body's body or
 * geometry via phys_body_rotate_mat3x3().
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
 * phys_body_rotate_xform() - set body's transform rotation
 * @body:   phys_body object
 * @xform:  transform to apply to the body
 *
 * Set the @body transform to @xform.
 */
void phys_body_rotate_xform(struct phys_body *body, transform_t *xform);

/**
 * phys_body_get_gravity() - get the world's gravitational acceleration
 * @body:       phys_body object
 * @gravity:    receiver for the gravity vector
 *
 * Retrieve the gravitational acceleration configured on the ODE world.
 */
void phys_body_get_gravity(struct phys_body *body, vec3 gravity);

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
 * phys_body_ground_collide() - test if body is on/in the ground
 * @body:       phys_body object
 * @grounded:   true if the body was grounded before this call
 *
 * Cast a single downward ray from the capsule bottom against the full
 * physics space.  If the ray hits within @body::yoffset distance, the
 * character is grounded and the Y position is corrected to place the
 * feet on the surface.  Sets the character's ground normal and
 * collision entity.
 *
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

/**
 * phys_body_get_mass() - get the body's total mass
 * @body:   phys_body object
 *
 * Return: the body's mass, or 0 if it has no dynamic body
 */
float phys_body_get_mass(struct phys_body *body);

/**
 * phys_body_push() - push a dynamic body with a force
 * @hit:            entity that was hit
 * @velocity:       velocity of the pusher at the time of contact
 * @pusher_mass:    mass of the pusher (used to scale the force)
 *
 * If @hit has a dynamic phys_body, apply a force proportional to
 * @pusher_mass * @velocity.  Has no effect on static geometries.
 */
void phys_body_push(entity3d *hit, const vec3 velocity, float pusher_mass);

/**
 * phys_body_sweep_capsule() - sweep a capsule along a delta vector
 * @body:       phys_body of the character (capsule)
 * @delta:      movement vector to sweep along
 * @normal:     [out] contact normal of the blocking surface
 * @hit_entity: [out] entity that was hit, can be NULL
 *
 * Temporarily move the capsule to the target position (current + @delta),
 * test for collisions against all physics spaces, and compute the fraction
 * of @delta that can be safely traveled before hitting something.
 *
 * The returned @normal points away from the obstacle, toward the capsule.
 *
 * Return: fraction [0.0, 1.0] of @delta that is unobstructed
 */
float phys_body_sweep_capsule(struct phys_body *body, const vec3 delta,
                              vec3 normal, entity3d **hit_entity);

#endif /* __CLAP_PHYSICS_H__ */
