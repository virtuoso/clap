/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_NAV_ROAMER_H__
#define __CLAP_NAV_ROAMER_H__

#include "error.h"
#include "linmath.h"
#include "util.h"
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
 * @radius:     the agent's half-width: paths keep this far off portal
 *              ends (see navmesh_path()), so a wide body walks the
 *              middle of a stair flight instead of hugging its rail;
 *              0 uses the mesh's navmesh_params::agent_radius
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
    float   radius;
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
 * @name:       shows in logs and the debug window; a static string the
 *              caller sets after roamer_init(), or nullptr
 * @tri:        last locate result, the search hint
 * @path:       the funnelled path being followed, @from first
 * @nr_path:    waypoints in @path; 0 when idle
 * @wp:         index of the waypoint currently steered toward
 * @idle_until: don't auto-pick again before this animation time
 * @yaw:        current heading, radians
 * @speed:      current speed, blended toward the target each tick
 * @ground:     ground height under the agent, maintained by
 *              roamer_update() as it moves, or by roamer_track()
 *              when something else moves the agent
 * @stuck:      seconds of no progress while trying to walk
 * @repathed:   the current stall already got its one recovery re-path
 * @entry:      on navmesh::roamers, the debug UI's registry
 * @dbg_pos:    the position roamer_update() last wrote, where the debug
 *              overlays draw
 * @show_path:  debug UI: draw the path being followed
 * @show_radius: debug UI: draw the pathing radius as a ring
 *
 * roamer_init() sets it up on a navmesh; seed @tri and @ground from the
 * spawn point (navmesh_random_point() returns the triangle) so the first
 * ground query and locate start warm.
 */
typedef struct roamer {
    navmesh         *nm;
    roamer_config   cfg;
    const char      *name;
    double          idle_until;
    int             tri;
    vec3            path[ROAMER_WAYPOINTS];
    int             nr_path;
    int             wp;
    float           yaw;
    float           speed;
    float           ground;
    float           stuck;
#ifndef CONFIG_FINAL
    vec3            dbg_pos;
    struct list     entry;
    bool            show_path;
    bool            show_radius;
#endif /* !CONFIG_FINAL */
    bool            repathed;
} roamer;

/**
 * roamer_init() - set a roamer up on a navmesh with the default behaviour
 * @r:      the roamer; fully overwritten
 * @nm:     a baked navmesh
 *
 * Registers the roamer on @nm for the debug UI (non-final builds), so a
 * live roamer needs roamer_done() before re-initializing it or freeing
 * the navmesh.
 */
void roamer_init(roamer *r, navmesh *nm);

/**
 * roamer_done() - unregister a roamer
 * @r:      the roamer
 *
 * Undoes roamer_init()'s registration; call before the navmesh is freed.
 * The roamer itself holds no allocations.
 */
void roamer_done(roamer *r);

/**
 * roamer_ground() - ground height probe, a gait_ground_fn over the navmesh
 * @priv:   the roamer (void * to match the humanoid ground callback)
 * @x, @z:  world coordinates
 *
 * Locates the triangle under (x, z) around the roamer's hint, within a
 * step of its ground, and evaluates its plane. A pure probe: the body
 * and the feet both ask, so it never moves the roamer's own tracking
 * state, and on a miss it holds the last height. Point humanoid::ground
 * here with humanoid::ground_priv set to the roamer.
 * Return: ground height at (x, z)
 */
float roamer_ground(void *priv, float x, float z);

/**
 * roamer_track() - follow a body that something else moves
 * @r:      the roamer
 * @pos:    current world position
 *
 * Advances the locate hint and the ground reference that
 * roamer_ground() probes around, without steering anything. A roamer
 * whose agent is driven from elsewhere (a player at the controls of a
 * humanoid, say) still wants this once per frame, on the body's own
 * position: nothing else moves the tracking state, and a stale one
 * strands the ground probe a storey behind. Like roamer_update(), it
 * looks within a step of the last ground, whatever the height of @pos.
 * Return: false if no walkable surface under @pos is within a step of
 * the last ground, tracking left where it was
 */
bool roamer_track(roamer *r, const vec3 pos);

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
 * The debug overlays and the roamer window live in core/nav/debug.c,
 * compiled for non-final builds only; the declarations gate to match.
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

/**
 * navmesh_roamers_debug() - the roamer debug window and overlays
 * @nm:     a baked navmesh
 * @scene:  scene whose message bus carries the overlay lines
 *
 * One panel per roamer registered on @nm: its live state, the path and
 * radius overlay toggles and the roamer_config knobs, plus a global
 * "show all paths". Call once per frame (the scene's debug pass does);
 * the overlays draw whether or not the window is open, so a toggled
 * path stays visible with the window closed.
 */
void navmesh_roamers_debug(navmesh *nm, struct scene *scene);
#endif /* CONFIG_FINAL */

#endif /* __CLAP_NAV_ROAMER_H__ */
