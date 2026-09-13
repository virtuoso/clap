/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_NAV_ROAMER_H__
#define __CLAP_NAV_ROAMER_H__

#include "error.h"
#include "linmath.h"
#include "navmesh.h"

/*
 * A self-steering agent on a baked navmesh: it picks a reachable destination,
 * follows the funnelled path there at a speed set by the distance remaining
 * (long haul runs, the approach brakes), stands around a bit, repeats. Feed
 * it a click destination with roamer_path_to() to drive it by hand instead.
 *
 * The roamer is motion only: roamer_update() reads the current world position
 * and writes the next one plus a heading, so the caller owns the entity. Point
 * a humanoid's steer and ground callbacks at one to walk a biped around a
 * level (see roamer_ground()); the two cruise speeds come from the humanoid's
 * walk and run tunings, passed per tick.
 */

/*
 * Waypoints a roamer follows: the funnel writes at most this many, and a
 * longer path ends at the last one that fits.
 */
#define ROAMER_WAYPOINTS    64

/**
 * struct roamer_config - roaming behaviour, fully parameterized
 * @min_dist:   auto-picked destinations sit at least this far away
 * @arrive:     a waypoint counts as reached within this radius
 * @turn_rate:  heading slew toward the next waypoint, rad/s
 * @accel:      speed approach rate, 1/s
 * @run_dist:   run while at least this much path remains
 * @slow_dist:  brake to a crawl within this distance of the destination
 * @auto_roam:  keep picking new destinations when idle; clear to only
 *              follow explicit roamer_path_to() targets
 *
 * roamer_config_init() writes the defaults; start from it and override.
 * Unlike the float fields, @auto_roam defaults on, so a zeroed config is
 * not the default - go through roamer_config_init().
 */
typedef struct roamer_config {
    float   min_dist;
    float   arrive;
    float   turn_rate;
    float   accel;
    float   run_dist;
    float   slow_dist;
    bool    auto_roam;
} roamer_config;

/**
 * roamer_config_init() - fill a roamer configuration with the defaults
 * @cfg:    the configuration; fully overwritten
 */
void roamer_config_init(roamer_config *cfg);

/**
 * struct roamer - a navmesh path-follower's state
 * @nm:         the baked navmesh it roams (required)
 * @cfg:        the roaming behaviour
 * @tri:        last locate result, the search hint
 * @path:       the funnelled path being followed, @from first
 * @nr_path:    waypoints in @path; 0 when idle
 * @wp:         index of the waypoint currently steered toward
 * @idle_until: don't auto-pick again before this animation time
 * @yaw:        current heading, radians
 * @speed:      current speed, blended toward the target each tick
 * @ground:     last known ground height under the agent
 * @stuck:      seconds of no progress while trying to walk
 *
 * roamer_init() sets it up on a navmesh; seed @tri and @ground from the
 * spawn point (navmesh_random_point() returns the triangle) so the first
 * ground query and locate start warm.
 */
typedef struct roamer {
    navmesh         *nm;
    roamer_config   cfg;
    int             tri;
    vec3            path[ROAMER_WAYPOINTS];
    int             nr_path;
    int             wp;
    double          idle_until;
    float           yaw;
    float           speed;
    float           ground;
    float           stuck;
} roamer;

/**
 * roamer_init() - set a roamer up on a navmesh with the default behaviour
 * @r:      the roamer; fully overwritten
 * @nm:     a baked navmesh
 */
void roamer_init(roamer *r, navmesh *nm);

/**
 * roamer_ground() - ground height probe, a gait_ground_fn over the navmesh
 * @priv:   the roamer (void * to match the humanoid ground callback)
 * @x, @z:  world coordinates
 *
 * Locates the triangle under (x, z) around the last hint and evaluates its
 * plane, updating roamer::tri and roamer::ground. On a miss it holds the
 * last height. Point humanoid::ground here with humanoid::ground_priv set
 * to the roamer.
 * Return: ground height at (x, z)
 */
float roamer_ground(void *priv, float x, float z);

/**
 * roamer_path_to() - path to a destination and start following it
 * @r:      the roamer
 * @pos:    current world position
 * @dest:   destination, world space (a click hit, say)
 *
 * Return: true if a usable path (at least two waypoints) was found
 */
bool roamer_path_to(roamer *r, const vec3 pos, const vec3 dest);

/**
 * roamer_update() - advance the roam state one tick
 * @r:          the roamer
 * @pos:        current world position
 * @t:          animation time, seconds
 * @dt:         seconds since the previous update
 * @walk_speed: cruising speed
 * @run_speed:  long-haul speed, used while roamer_config::run_dist remains
 * @out_pos:    receives the next world position
 * @out_yaw:    receives the next heading, radians (always written)
 *
 * Picks a destination when idle (if roamer_config::auto_roam), steps along
 * the path bounded by the turn rate and acceleration, and recovers from
 * being stuck by re-picking. Only the horizontal position moves; the caller
 * keeps Y (a humanoid overwrites it with the ground height).
 * Return: true when the agent should be placed at @out_pos this tick
 */
bool roamer_update(roamer *r, const vec3 pos, double t, float dt,
                   float walk_speed, float run_speed, vec3 out_pos, float *out_yaw);

/*
 * The path overlay lives in core/nav/debug.c alongside navmesh_debug_draw().
 * It is gated on CONFIG_FINAL rather than that file's CLAP_NAVMESH_DEBUG:
 * targets outside the engine are not built with CLAP_NAVMESH_DEBUG, but
 * they do see CONFIG_FINAL from config.h. The two conditions coincide -
 * debug.c is compiled only for non-final builds.
 */
#ifndef CONFIG_FINAL
struct scene;

/**
 * roamer_draw_path() - debug-draw the path being followed
 * @r:      the roamer
 * @scene:  scene whose message bus carries the debug lines
 * @pos:    current world position, where the first segment starts
 *
 * Emits the remaining path as debug lines and marks the destination.
 */
void roamer_draw_path(roamer *r, struct scene *scene, const vec3 pos);
#endif /* !CONFIG_FINAL */

#endif /* __CLAP_NAV_ROAMER_H__ */
