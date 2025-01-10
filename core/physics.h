/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PHYSICS_H__
#define __CLAP_PHYSICS_H__

#ifdef __EMSCRIPTEN__
#define dSINGLE
#else
#define dDOUBLE
#endif
#include <ode/ode.h>
#include "linmath.h"

enum {
    PHYS_BODY = 0,
    PHYS_GEOM,
};

struct phys_body;

typedef void (*ground_contact_fn)(void *priv, float x, float y, float z);

struct phys;
extern struct phys *phys;

struct entity3d;

typedef struct phys_contact_params {
    float   bounce;
    float   bounce_vel;
} phys_contact_params;

/* Set body's contact parameters */
#define phys_body_set_contact_params(_b, args...) \
    _phys_body_set_contact_params((_b), &(phys_contact_params){ args })
void _phys_body_set_contact_params(struct phys_body *body, const phys_contact_params *params);

void phys_step(unsigned long frame_count);
int  phys_init(void);
void phys_done(void);
/* Set the global ground_contact callback */
void phys_set_ground_contact(struct phys *phys, ground_contact_fn ground_contact);
void phys_ground_add(struct entity3d *e);
struct entity3d *phys_ray_cast(struct entity3d *e, vec3 start, vec3 dir, double *pdist);
/* Check if phys_body has a body (true) or just a geometry (false) */
bool phys_body_has_body(struct phys_body *body);
struct entity3d *phys_body_entity(struct phys_body *body);
const dReal *phys_body_position(struct phys_body *body);
const dReal *phys_body_rotation(struct phys_body *body);
struct phys_body *phys_body_new(struct phys *phys, struct entity3d *entity, int class,
                                double geom_radius, double geom_offset, int type, double mass);
int phys_body_update(struct entity3d *e);
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
void phys_ground_entity(struct entity3d *e);

struct scene;
void phys_debug_draw(struct scene *scene, struct phys_body *body);

#endif /* __CLAP_PHYSICS_H__ */
