/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_GAIT_H__
#define __CLAP_PROCANIM_GAIT_H__

#include "error.h"
#include "linmath.h"

/**
 * struct gait_leg - per-leg gait state
 * @target:     current foot target, world space; aim the leg's IK at it
 * @offset:     foot offset from the body center in the body frame:
 *              [0] along the right axis, [2] along forward, [1] unused
 * @phase:      the leg's phase offset within the cycle, [0, 1)
 * @swing_from: swing start point (internal)
 * @swing_to:   predicted landing point (internal)
 * @swinging:   the foot is airborne
 *
 * Fill @offset and @phase before gait_reset(); read @target and
 * @swinging after every gait_update().
 */
typedef struct gait_leg {
    vec3    target;
    vec3    offset;
    float   phase;
    vec3    swing_from;
    vec3    swing_to;
    bool    swinging;
} gait_leg;

/**
 * typedef gait_ground_fn - ground height probe
 * @priv:   gait::ground_priv
 * @x:      world X coordinate
 * @z:      world Z coordinate
 *
 * Return: the walkable ground height at (x, z)
 */
typedef float (*gait_ground_fn)(void *priv, float x, float z);

/**
 * struct gait - a walking gait over a set of legs
 * @legs:           caller-owned leg array (required)
 * @nr_legs:        number of legs
 * @cycle:          seconds per full gait cycle; 0 picks the default
 * @swing:          fraction of the cycle each foot is airborne; 0:
 *                  default
 * @stride:         distance between successive footfalls of one leg;
 *                  0: default
 * @step_height:    swing arc height; 0 picks the default
 * @foot_y:         planted foot target height above the ground
 * @phase:          the accumulated cycle phase (internal)
 * @ground:         ground height probe; nullptr: flat ground at 0
 * @ground_priv:    the probe's own state (a navmesh, a terrain)
 *
 * The gait owns when and where feet step: planted feet hold their
 * world anchor, swinging feet arc toward a landing spot predicted from
 * the body's motion (body position at touchdown, half a stride ahead,
 * offset to the leg's side). The caller owns the body itself: drive
 * position, heading and speed however the character moves, call
 * gait_update() once per frame and solve each leg's IK toward
 * gait_leg::target. Frame time in, world-space targets out;
 * deterministic for a given body path. The phase integrates
 * incrementally, so every parameter can change between updates
 * (walk-to-run blending) without teleporting the feet.
 */
typedef struct gait {
    gait_leg        *legs;
    size_t          nr_legs;
    float           cycle;
    float           swing;
    float           stride;
    float           step_height;
    float           foot_y;
    float           phase;
    gait_ground_fn  ground;
    void            *ground_priv;
} gait;

/**
 * gait_reset() - plant all feet at their rest spots
 * @g:          the gait
 * @pos:        body center, world space
 * @forward:    body forward axis, unit
 * @right:      body right axis, unit
 *
 * Plant every foot at its body-frame offset from @pos, gait::foot_y
 * above the ground, and clear the swing state. Call once after placing
 * the body.
 */
void gait_reset(gait *g, const vec3 pos, const vec3 forward, const vec3 right);

/**
 * gait_update() - advance the gait one frame
 * @g:          the gait
 * @pos:        body center, world space
 * @forward:    body forward axis, unit
 * @right:      body right axis, unit
 * @speed:      body speed along @forward
 * @dt:         seconds since the previous update
 *
 * Update every leg's target: planted feet stay put, feet entering
 * their swing window lift off toward a landing predicted from @speed,
 * arcing by gait::step_height. The landing prediction is linear in the
 * heading: fine for gentle arcs, increasingly wrong for tight turns.
 * Swing fractions above 0.5 overlap the legs' windows into flight
 * phases: that is how running works.
 * Context: the body owner's update callback
 * Return: CERR_OK or CERR_INVALID_ARGUMENTS
 */
cerr_check gait_update(gait *g, const vec3 pos, const vec3 forward, const vec3 right,
                       float speed, float dt);

#endif /* __CLAP_PROCANIM_GAIT_H__ */
