/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_NAVMESH_H__
#define __CLAP_NAVMESH_H__

#include "error.h"
#include "linmath.h"

/**
 * struct navmesh_params - navigation mesh build parameters
 * @max_slope:      steepest walkable surface, radians from horizontal;
 *                  0 picks the default (40 degrees)
 * @weld:           vertex weld radius: triangles whose corners fall
 *                  within it are stitched together; 0: default (0.01)
 * @step_height:    obstructions rising less than this above a floor's
 *                  top don't block it; 0: default (0.25)
 * @agent_height:   headroom required above a floor; 0: default (2.0)
 * @agent_radius:   obstructions closer than this sideways block the
 *                  floor, and path corners keep this far from portal
 *                  ends; 0: default (0.5)
 *
 * All floor candidates steeper than @max_slope become obstructions, as
 * do sources added with navmesh_source::obstacle set. A floor triangle
 * survives baking when no obstruction cuts into the space between
 * @step_height and @agent_height above it, within @agent_radius
 * sideways. Another floor overhead only obstructs when it leaves less
 * than @agent_height of headroom: walkable slopes don't shadow their
 * own feet, bridges don't block what's underneath unless too low.
 */
typedef struct navmesh_params {
    float   max_slope;
    float   weld;
    float   step_height;
    float   agent_height;
    float   agent_radius;
} navmesh_params;

typedef struct navmesh navmesh;
cresp_ret(navmesh);

/**
 * navmesh_new() - allocate an empty navigation mesh
 * @args:   comma-separated build parameter initializers
 *          (see &struct navmesh_params)
 *
 * Feed it geometry with navmesh_add(), finalize with navmesh_bake().
 * Return: the navmesh or CERR_NOMEM
 */
#define navmesh_new(args...) \
    _navmesh_new(&(navmesh_params){ args })
cresp_check(navmesh) _navmesh_new(const navmesh_params *params);

/**
 * navmesh_free() - free a navigation mesh
 * @nm:     the navmesh, or nullptr
 */
void navmesh_free(navmesh *nm);

/**
 * struct navmesh_source - one batch of collision geometry
 * @vx:         vertex positions, xyz triplets, model space
 * @idx:        triangle indices into @vx
 * @vxsz:       size of @vx in bytes (matches model3d::collision_vxsz)
 * @idxsz:      size of @idx in bytes (matches model3d::collision_idxsz)
 * @pos:        world translation (vec3); nullptr: origin
 * @rot:        world rotation (quat); nullptr: identity
 * @scale:      uniform scale; 0: 1.0
 * @obstacle:   never walkable, and denies the floor it covers at any
 *              height (lava, hazards); unmarked geometry only
 *              obstructs above navmesh_params::step_height
 *
 * World transform convention matches the physics trimesh:
 * world = rot * (scale * v) + pos.
 */
typedef struct navmesh_source {
    const float             *vx;
    const unsigned short    *idx;
    size_t                  vxsz;
    size_t                  idxsz;
    const float             *pos;
    const float             *rot;
    float                   scale;
    bool                    obstacle;
} navmesh_source;

/**
 * navmesh_add() - add collision geometry to an unbaked navmesh
 * @_nm:    the navmesh
 * @args:   comma-separated source initializers (see &struct navmesh_source)
 *
 * Triangles flatter than navmesh_params::max_slope become floor
 * candidates, everything else obstructs.
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS (missing or ragged geometry)
 * or CERR_INVALID_OPERATION (already baked)
 */
#define navmesh_add(_nm, args...) \
    _navmesh_add((_nm), &(navmesh_source){ args })
cerr_check _navmesh_add(navmesh *nm, const navmesh_source *source);

/**
 * navmesh_bake() - finalize the navmesh around a seed point
 * @nm:     the navmesh with all sources added
 * @seed:   a point known to be on walkable ground; only floor
 *          connected to it survives
 *
 * Welds floor triangles into a connected mesh, culls the obstructed
 * ones and everything unreachable from @seed. Frees the input
 * geometry: navmesh_add() is no longer accepted.
 * Return: CERR_OK, CERR_INVALID_OPERATION (already baked) or
 * CERR_NOT_FOUND (no walkable floor at or around @seed)
 */
cerr_check navmesh_bake(navmesh *nm, const vec3 seed);

/**
 * navmesh_bake_largest() - finalize keeping the biggest walkable region
 * @nm:     the navmesh with all sources added
 *
 * Like navmesh_bake() but keeps the largest connected component instead
 * of the one under a seed point: for scenes whose walkable ground is
 * scattered and whose best floor the caller can't name in advance.
 * Pair with navmesh_random_point() to place an agent on it.
 * Return: CERR_OK, CERR_INVALID_OPERATION (already baked) or
 * CERR_NOT_FOUND (no walkable floor at all)
 */
cerr_check navmesh_bake_largest(navmesh *nm);

/**
 * navmesh_nr_tris() - the number of walkable triangles
 * @nm:     the navmesh
 *
 * Return: the number of walkable triangles; 0 until baked
 */
size_t navmesh_nr_tris(navmesh *nm);

/**
 * navmesh_locate() - find the walkable triangle under a point
 * @nm:     a baked navmesh
 * @p:      the point, world space
 * @max_dy: vertical tolerance; 0: unlimited
 * @hint:   a triangle to search around first (an earlier locate result),
 *          -1: none
 *
 * Of the triangles containing (x, z), picks the one closest to the
 * point vertically.
 * Return: triangle index or CERR_NOT_FOUND
 */
cres(int) navmesh_locate(navmesh *nm, const vec3 p, float max_dy, int hint);

/**
 * navmesh_height() - ground height on a triangle
 * @nm:     a baked navmesh
 * @tri:    triangle index (from navmesh_locate())
 * @x:      world X coordinate
 * @z:      world Z coordinate
 *
 * Evaluates the triangle's plane: (x, z) slightly outside the triangle
 * extrapolates it.
 * Return: ground height at (x, z)
 */
float navmesh_height(navmesh *nm, int tri, float x, float z);

/**
 * navmesh_random_point() - pick a point uniformly on the walkable area
 * @nm:         a baked navmesh
 * @r1:         uniform random number in [0, 1)
 * @r2:         uniform random number in [0, 1)
 * @r3:         uniform random number in [0, 1)
 * @pt:         receives the point, world space
 *
 * The caller owns the randomness (tests pass constants).
 * Return: the triangle index @pt lies on, or CERR_INVALID_OPERATION if
 *         @nm isn't baked
 */
cres(int) navmesh_random_point(navmesh *nm, float r1, float r2, float r3, vec3 pt);

/**
 * navmesh_path() - find a walkable path between two points
 * @nm:             a baked navmesh
 * @from:           start point, world space
 * @to:             destination, world space
 * @waypoints:      receives the path, @from first, @to last
 * @nr_waypoints:   capacity of @waypoints, at least 2
 *
 * A* over the triangle graph, then funnel straightening; corners keep
 * navmesh_params::agent_radius away from portal ends. If the full path
 * doesn't fit, the first @nr_waypoints of it are still written: walk
 * them and ask again.
 * Return: the number of waypoints written or CERR_NOT_FOUND (either
 * point off the mesh) or CERR_INVALID_ARGUMENTS
 */
cres(int) navmesh_path(navmesh *nm, const vec3 from, const vec3 to,
                       vec3 *waypoints, int nr_waypoints);

/**
 * navmesh_tri() - triangle corners, for debugging and visualization
 * @nm:     a baked navmesh
 * @tri:    triangle index
 * @c:      receives the three corners
 *
 * Return: CERR_OK, or CERR_INVALID_ARGUMENTS for a triangle that isn't
 *         there
 */
cerr navmesh_tri(navmesh *nm, int tri, vec3 c[3]);

#endif /* __CLAP_NAVMESH_H__ */
