/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_FABRIK_H__
#define __CLAP_FABRIK_H__

#include "error.h"
#include "linmath.h"

/**
 * struct fabrik_solve_opts - FABRIK inverse kinematics solver options
 * @points:         joint positions, solved in place (required)
 * @lengths:        @nr - 1 segment rest lengths, all positive (required)
 * @nr:             number of points, at least 2
 * @target:         where the last point reaches for
 * @pole:           bend side hint: the chain's slack bows this way; only
 *                  the direction matters, zero to skip
 * @max_iterations: iteration cap; 0 picks the default
 * @tolerance:      acceptable distance from the last point to @target;
 *                  0 picks a default relative to the chain length
 *
 * The first point stays anchored where it came in; the rest are moved so
 * that segment lengths hold and the last point lands on (or as close as
 * possible to) @target. Without a pole, @points double as the seed
 * configuration and the solver converges to a solution near it; with
 * one, the interior points are re-seeded onto a chain-length circular
 * arc over the root-to-target chord, bowed to the pole's side, which
 * pins the fold side no matter where the chain came in. Give @pole
 * whenever the target can pull the chain into a fold; the component of
 * @pole parallel to the root-to-target line is ignored. Three points
 * with a pole (a leg, an arm) are solved in closed form: exact, the
 * same for the same input, no iterations.
 */
typedef struct fabrik_solve_opts {
    vec3            *points;
    const float     *lengths;
    size_t          nr;
    vec3            target;
    vec3            pole;
    unsigned int    max_iterations;
    float           tolerance;
} fabrik_solve_opts;

/**
 * _fabrik_solve() - solve an IK chain in place with FABRIK
 * @opts:   solver options
 *
 * Return: CERR_OK, or CERR_INVALID_ARGUMENTS for missing arrays, fewer
 *         than 2 points or a segment length that isn't positive
 */
cerr_check _fabrik_solve(const fabrik_solve_opts *opts);

/**
 * define fabrik_solve - solve an IK chain in place with FABRIK
 *
 * Syntax sugar for _fabrik_solve(), taking fabrik_solve_opts initializers.
 */
#define fabrik_solve(args...) _fabrik_solve(&(fabrik_solve_opts){ args })

#endif /* __CLAP_FABRIK_H__ */
