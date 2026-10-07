/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_SLAB_H__
#define __CLAP_PCG_SLAB_H__

#include "error.h"
#include "linmath.h"
#include "triangulate.h"
#include "pcg/sink.h"
#include "pcg/surface.h"

/**
 * enum pcg_slab_surf - pcg_slab_params surfaces
 * @PCG_SLAB_TOP:           the structure's top; only without a finish
 * @PCG_SLAB_SOFFIT:        the structure's underside
 * @PCG_SLAB_EDGE:          the structure's sides around every ring
 * @PCG_SLAB_FINISH:        the walk finish's top
 * @PCG_SLAB_FINISH_EDGE:   the walk finish's sides
 * @PCG_SLAB_NR_SURF:       number of surfaces
 */
enum pcg_slab_surf {
    PCG_SLAB_TOP,
    PCG_SLAB_SOFFIT,
    PCG_SLAB_EDGE,
    PCG_SLAB_FINISH,
    PCG_SLAB_FINISH_EDGE,
    PCG_SLAB_NR_SURF,
};

/**
 * struct pcg_slab_params - a floor slab's style
 * @t:          structure thickness, hanging below the site's y = 0
 * @finish_t:   walk finish thickness on top of the structure, 0 for none
 * @surf:       per-surface finish, also by name
 *
 * A structural plate over [-@t, 0] with an optional walk finish over
 * [0, @finish_t] laid on it: a deck on a concrete slab, or a roof slab
 * without one. With a finish, the structure's top is never emitted: it
 * would sit flush under the finish, invisible, and bake as a phantom
 * walkable sheet under the real floor.
 */
typedef struct pcg_slab_params {
    float   t;
    float   finish_t;
    union {
        pcg_surface surf[PCG_SLAB_NR_SURF];
        struct {
            pcg_surface top, soffit, edge, finish, finish_edge;
        };
    };
} pcg_slab_params;

/**
 * pcg_slab_params_init() - a generic slab's style
 * @p:  the params to fill, every field
 *
 * A 0.3 m slab with a 0.05 m walk finish. The structure's surfaces are
 * PCG_ROLE_STRUCTURE, the finish's PCG_ROLE_FLOOR, all PCG_COLLIDE_SOLID.
 */
void pcg_slab_params_init(pcg_slab_params *p);

/**
 * struct pcg_slab_site - where a slab goes
 * @frame:      the slab's frame; its y is the top of the structure
 * @pts:        outline and hole points in the frame's (a, c)
 * @nr_pts:     number of points
 * @rings:      the outline and its holes, in any order and winding (see
 *              poly_triangulate()); nullptr: one ring of all of @pts
 * @nr_rings:   number of @rings
 *
 * The finish covers the same outline as the structure. Top and bottom
 * faces chart the sink-space (x, z) in meters, so slabs and other plates
 * of one material tile across each other; side faces run U along each
 * ring edge and V up the local y.
 */
typedef struct pcg_slab_site {
    pcg_frame       frame;
    const vec2      *pts;
    size_t          nr_pts;
    const poly_ring *rings;
    size_t          nr_rings;
} pcg_slab_site;

/**
 * pcg_slab_check() - check a slab's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * The checks every emit makes before it emits anything. A parent that
 * places a slab among other children makes them before any of its
 * children emits, so that a bad slab leaves nothing behind. The outline
 * itself is pcg_plate()'s to turn down, when it is emitted.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * thickness that isn't positive and finite, a finish thickness that is
 * negative or isn't finite, a frame pcg_frame_check() turns down or a
 * point that isn't finite
 */
cerr_check pcg_slab_check(const pcg_slab_params *p, const pcg_slab_site *site);

/**
 * _pcg_slab_emit() - emit a slab
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx, for what
 * pcg_slab_check() turns down or an outline pcg_plate() turns down; the
 * pcg_plate() errors
 */
cerr_check _pcg_slab_emit(const pcg_ctx *ctx, const pcg_slab_params *p, const pcg_slab_site *site);

/**
 * define pcg_slab_emit - emit a slab at a site described by pcg_slab_site initializers
 *
 * Syntax sugar for _pcg_slab_emit().
 */
#define pcg_slab_emit(_ctx, _p, args...) _pcg_slab_emit((_ctx), (_p), &(const pcg_slab_site){ args })

#endif /* __CLAP_PCG_SLAB_H__ */
