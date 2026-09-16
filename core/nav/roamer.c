// SPDX-License-Identifier: Apache-2.0
/*
 * Navmesh roamer: a self-steering agent that picks reachable destinations and
 * follows the funnelled path to them, or a hand-fed one from roamer_path_to().
 *
 * Pure motion over the navmesh - no entity, no render. roamer_update() reads a
 * position and writes the next, so a humanoid (or anything) can be its body.
 * The debug path overlay lives in debug.c, which reaches into the scene.
 */
#include <math.h>
#include <stdlib.h>
#include "common.h"
#include "interp.h"
#include "logger.h"
#include "util.h"
#include "navmesh.h"
#include "navmesh-internal.h"   /* navmesh::params, the step_height bound */
#include "nav/roamer.h"

#define ROAMER_MIN_DIST     5.0f    /* destinations at least this far away */
#define ROAMER_ARRIVE       0.45f   /* waypoint switch radius */
#define ROAMER_TURN_RATE    2.2f    /* rad/s */
#define ROAMER_ACCEL        1.6f    /* speed approach rate, 1/s */
#define ROAMER_RUN_DIST     7.0f    /* run while this much path remains */
#define ROAMER_SLOW_DIST    1.4f    /* brake zone before the destination */

void roamer_config_init(roamer_config *cfg)
{
    *cfg = (roamer_config){
        .min_dist   = ROAMER_MIN_DIST,
        .arrive     = ROAMER_ARRIVE,
        .turn_rate  = ROAMER_TURN_RATE,
        .accel      = ROAMER_ACCEL,
        .run_dist   = ROAMER_RUN_DIST,
        .slow_dist  = ROAMER_SLOW_DIST,
        .auto_roam  = true,
    };
}

void roamer_init(roamer *r, navmesh *nm)
{
    *r = (roamer){ .nm = nm };
    roamer_config_init(&r->cfg);
#ifndef CONFIG_FINAL
    list_append(&nm->roamers, &r->entry);
#endif /* !CONFIG_FINAL */
}

void roamer_done(roamer *r)
{
#ifndef CONFIG_FINAL
    list_del(&r->entry);
#endif /* !CONFIG_FINAL */
}

/* roamers log and display by name once the caller sets one */
static const char *roamer_name(const roamer *r)
{
    return r->name ? r->name : "roamer";
}

float roamer_ground(void *priv, float x, float z)
{
    roamer *r = priv;

    /*
     * The body and the gait's feet both probe through here; only
     * roamer_update() moves the tracking state. A foot planted a leg
     * span out can rest on a surface the body is not on - the deck
     * below a flight of stairs, a tread overhead - and dragging the
     * hint and the ground reference along with it would teleport the
     * body there on its next locate. Within a step of the last ground
     * for the same reason: anything further is a stacked surface, not
     * walkable continuity.
     */
    int tri = CRES_RET(
        navmesh_locate(r->nm, (vec3){ x, r->ground, z },
                       r->nm->params.step_height, r->tri),
        return r->ground;
    );

    return navmesh_height(r->nm, tri, x, z);
}

bool roamer_path_to(roamer *r, const vec3 pos, const vec3 dest)
{
    auto nr = CRES_RET(
        navmesh_path(r->nm, pos, dest, r->cfg.radius, r->path, ROAMER_WAYPOINTS),
        return false
    );

    if (nr < 2) return false;

    r->nr_path = nr;
    r->wp = 1;
    r->stuck = 0.0f;
    r->repathed = false;
    trace("%s to (%5.2f %5.2f %5.2f), %d waypoints\n",
        roamer_name(r), dest[0], dest[1], dest[2], nr);
    return true;
}

/* auto-pick a destination worth walking to and start following it */
static bool roamer_pick(roamer *r, const vec3 pos, double t)
{
    for (int i = 0; i < 12; i++) {
        vec3 dest, best = { 0 };
        bool have = false;

        /*
         * Sample a few candidates and lean toward higher ground: the
         * walkable area is mostly the flat plane, so an unbiased pick
         * almost never sends the agent up a slope. Take the highest of a
         * handful that are far enough to be worth the trip.
         */
        for (int s = 0; s < 5; s++) {
            CRES_RET(navmesh_random_point(r->nm, drand48(), drand48(), drand48(), dest), continue);

            float dx = dest[0] - pos[0], dz = dest[2] - pos[2];
            if (dx * dx + dz * dz < r->cfg.min_dist * r->cfg.min_dist)  continue;
            if (!have || dest[1] > best[1]) {
                vec3_dup(best, dest);
                have = true;
            }
        }
        if (have && roamer_path_to(r, pos, best))   return true;
    }

    r->idle_until = t + 1.0;
    return false;
}

bool roamer_update(roamer *r, const vec3 pos, double t, float dt,
                   float walk_speed, float run_speed, vec3 out_pos, float *out_yaw)
{
    float target_speed = 0.0f;
    bool moved = false;

    if (!r->nr_path && r->cfg.auto_roam && t >= r->idle_until)  roamer_pick(r, pos, t);

    if (r->nr_path) {
        vec3 d = { r->path[r->wp][0] - pos[0], 0.0f, r->path[r->wp][2] - pos[2] };
        float dist = sqrtf(d[0] * d[0] + d[2] * d[2]);

        /* hop waypoints as they come within reach */
        while (dist < r->cfg.arrive && r->wp + 1 < r->nr_path) {
            r->wp++;
            d[0] = r->path[r->wp][0] - pos[0];
            d[2] = r->path[r->wp][2] - pos[2];
            dist = sqrtf(d[0] * d[0] + d[2] * d[2]);
        }

        if (dist < r->cfg.arrive) {
            trace("%s arrived at (%5.2f %5.2f), idling\n",
                roamer_name(r), pos[0], pos[2]);
            r->nr_path = 0;
            r->idle_until = t + 2.0 + drand48() * 3.0;
        } else {
            float remaining = dist;

            for (int i = r->wp + 1; i < r->nr_path; i++) {
                float sx = r->path[i][0] - r->path[i - 1][0];
                float sz = r->path[i][2] - r->path[i - 1][2];

                remaining += sqrtf(sx * sx + sz * sz);
            }

            target_speed = remaining > r->cfg.run_dist  ? run_speed :
                           remaining > r->cfg.slow_dist ? walk_speed :
                                                          walk_speed * 0.55f;

            /* turn toward the waypoint at a bounded rate */
            float want = atan2f(d[0], d[2]);
            float dyaw = want - r->yaw;

            dyaw -= 2.0f * (float)M_PI * roundf(dyaw / (2.0f * (float)M_PI));
            r->yaw += clampf(dyaw, -r->cfg.turn_rate * dt, r->cfg.turn_rate * dt);

            /*
             * Only commit forward once roughly facing the waypoint;
             * otherwise turn in place. A yaw that lags around a corner
             * would otherwise drive the body straight into the wall the
             * path is meant to skirt.
             */
            target_speed *= fmaxf(0.0f, cosf(want - r->yaw));
        }
    }

    r->speed = linf_interp(r->speed, target_speed, approachf(r->cfg.accel, dt));

    vec3_dup(out_pos, pos);
    if (r->speed > 1e-3f && r->nr_path) {
        /*
         * Move toward the waypoint itself, not along the (lagging)
         * facing: the funnel guarantees the straight run to the next
         * waypoint stays on the mesh, so this follows the corridor
         * instead of cutting into the wall at a corner. The body still
         * turns to face where it goes; the animation just lags a step.
         */
        vec3 mv = { r->path[r->wp][0] - pos[0], 0.0f, r->path[r->wp][2] - pos[2] };
        float ml = hypotf(mv[0], mv[2]);

        if (ml > 1e-4f) {
            float step = fminf(r->speed * dt, ml);
            float fx = mv[0] / ml, fz = mv[2] / ml;
            vec3 next = { pos[0] + fx * step, pos[1], pos[2] + fz * step };
            /*
             * Accept at most a step of rise or drop: a surface further
             * away vertically is another storey stacked over this one
             * (a stair flight over the deck it climbs from), and
             * snapping to it teleports the body through the geometry.
             */
            cres(int) tri = navmesh_locate(r->nm, (vec3){ next[0], r->ground, next[2] },
                                           r->nm->params.step_height, r->tri);

            /*
             * A miss usually means the step grazed a notch the
             * clearance cull ate from the walkable edge - the funnel
             * cuts corners tighter than the ragged boundary, all the
             * more with a wide agent's shrunken portals. Probe half a
             * step ahead, then a full step with half a step of
             * sideways slide, and keep whatever stays on the mesh; if
             * everything misses, hold and retry - never freeze hard.
             */
            if (IS_CERR(tri)) {
                vec3 cand[3] = {
                    { pos[0] + fx * step * 0.5f, pos[1], pos[2] + fz * step * 0.5f },
                    { next[0] - fz * step * 0.5f, pos[1], next[2] + fx * step * 0.5f },
                    { next[0] + fz * step * 0.5f, pos[1], next[2] - fx * step * 0.5f },
                };

                for (int i = 0; i < 3 && IS_CERR(tri); i++) {
                    tri = navmesh_locate(r->nm, (vec3){ cand[i][0], r->ground, cand[i][2] },
                                         r->nm->params.step_height, r->tri);
                    if (!IS_CERR(tri))  vec3_dup(next, cand[i]);
                }
            }

            if (!IS_CERR(tri)) {
                vec3_dup(out_pos, next);
                r->tri = tri.val;
                r->ground = navmesh_height(r->nm, tri.val, next[0], next[2]);
                moved = true;
            }
        }
    }

    /*
     * Stuck recovery: if the body wants to walk but barely moves for a
     * while (jammed in a corner, or the path clips geometry the mesh
     * doesn't model), drop the path and pick a fresh destination.
     */
    float progress = hypotf(out_pos[0] - pos[0], out_pos[2] - pos[2]);

    if (r->nr_path && target_speed > 0.05f && progress < target_speed * dt * 0.25f) {
        r->stuck += dt;

        /*
         * Waypoint hopping trades path exactness for smooth motion, so
         * the body sometimes cuts a corner off the corridor and runs
         * against a hole the mesh never covered. Before writing the
         * journey off, re-path once to the same destination from where
         * the body actually stands: the fresh funnel starts here, on
         * the mesh.
         */
        if (!r->repathed && r->stuck >= 0.4f) {
            vec3 dest;

            vec3_dup(dest, r->path[r->nr_path - 1]);
            if (roamer_path_to(r, out_pos, dest)) {
                trace("%s snagged at (%5.2f %5.2f), re-pathing\n",
                    roamer_name(r), out_pos[0], out_pos[2]);
                r->repathed = true;
            }
        }
    } else {
        r->stuck = 0.0f;
    }

    if (r->stuck > 1.5f) {
        trace("%s stuck at (%5.2f %5.2f), re-picking\n",
            roamer_name(r), out_pos[0], out_pos[2]);
        r->nr_path = 0;
        r->idle_until = t + 0.3;
        r->stuck = 0.0f;
    }

#ifndef CONFIG_FINAL
    vec3_dup(r->dbg_pos, out_pos);
#endif /* !CONFIG_FINAL */

    *out_yaw = r->yaw;
    return moved;
}
