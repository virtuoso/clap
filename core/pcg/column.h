/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_COLUMN_H__
#define __CLAP_PCG_COLUMN_H__

#include "error.h"
#include "linmath.h"
#include "pcg/sink.h"
#include "pcg/surface.h"

/**
 * enum pcg_column_surf - pcg_column_params surfaces
 * @PCG_COLUMN_SHAFT:   the shaft, all faces
 * @PCG_COLUMN_BASE:    the base block at its foot, all faces
 * @PCG_COLUMN_NR_SURF: number of surfaces
 */
enum pcg_column_surf {
    PCG_COLUMN_SHAFT,
    PCG_COLUMN_BASE,
    PCG_COLUMN_NR_SURF,
};

/**
 * struct pcg_column_params - a square column's style
 * @w:          shaft side
 * @base_w:     side of the base block around the shaft's foot, wider
 *              than @w; 0 together with @base_h for no base
 * @base_h:     base block height
 * @top_gap:    the shaft stops this far below the top of the site's
 *              height, inside the slab it carries: a cap flush with the
 *              slab's top would fight the floor laid on it
 * @surf:       per-surface finish, also by name
 *
 * A shaft of @w square standing on the site's floor, centered on the
 * frame's origin and squared to its run direction, with an optional base
 * block. Every face charts the column's own local meters, so all columns
 * of one style look alike wherever they stand.
 */
typedef struct pcg_column_params {
    float   w;
    float   base_w;
    float   base_h;
    float   top_gap;
    union {
        pcg_surface surf[PCG_COLUMN_NR_SURF];
        struct {
            pcg_surface shaft, base;
        };
    };
} pcg_column_params;

/**
 * pcg_column_params_init() - a generic column's style
 * @p:  the params to fill, every field
 *
 * A 0.4 m shaft on a 0.54 m base 0.45 m tall, stopping 0.02 m short of
 * its top. The shaft is PCG_ROLE_CLADDING, the base PCG_ROLE_STRUCTURE,
 * both PCG_COLLIDE_SOLID.
 */
void pcg_column_params_init(pcg_column_params *p);

/**
 * struct pcg_column_site - where a column goes
 * @frame:  the foot of the shaft, at its center; the faces square to the
 *          frame's run direction
 * @h:      from the foot to the top of the slab the column carries; the
 *          shaft is @h less the style's top_gap
 */
typedef struct pcg_column_site {
    pcg_frame   frame;
    float       h;
} pcg_column_site;

/**
 * pcg_column_check() - check a column's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * The checks every emit makes before it emits anything. A parent that
 * places a column among other children makes them before any of its
 * children emits, so that a bad column leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a dimension or height that isn't
 * finite, a shaft side that isn't positive, a base without both a side
 * and a height or narrower than the shaft, a negative top gap, or a
 * height that leaves no shaft above the gap or no shaft above the base
 */
cerr_check pcg_column_check(const pcg_column_params *p, const pcg_column_site *site);

/**
 * _pcg_column_emit() - emit a column
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx and for what
 * pcg_column_check() turns down; the pcg_sink_reserve() errors
 */
cerr_check _pcg_column_emit(const pcg_ctx *ctx, const pcg_column_params *p, const pcg_column_site *site);

/**
 * define pcg_column_emit - emit a column at a site described by pcg_column_site initializers
 *
 * Syntax sugar for _pcg_column_emit().
 */
#define pcg_column_emit(_ctx, _p, args...) _pcg_column_emit((_ctx), (_p), &(const pcg_column_site){ args })

/**
 * struct pcg_columns_site - where a set of like columns go
 * @frame:  the floor they stand on; their faces square to its run
 *          direction
 * @at:     the feet in the frame's (a, c)
 * @nr_at:  number of columns
 * @h:      see &pcg_column_site.h, the same for all of them
 *
 * A colonnade or a ring of columns batched into the same buckets: one
 * draw per material and chunk, not one per column.
 */
typedef struct pcg_columns_site {
    pcg_frame   frame;
    const vec2  *at;
    size_t      nr_at;
    float       h;
} pcg_columns_site;

/**
 * pcg_columns_check() - check a set of columns' style and fit
 * @p:      the style
 * @site:   the fit
 *
 * The checks every emit makes before it emits anything. A parent that
 * places columns among other children makes them before any of its
 * children emits, so that bad columns leave nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, no columns, a foot that isn't
 * finite and for what pcg_column_check() turns down in the style and the
 * height
 */
cerr_check pcg_columns_check(const pcg_column_params *p, const pcg_columns_site *site);

/**
 * _pcg_columns_emit() - emit a set of columns
 * @ctx:    where they go
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx and for what
 * pcg_columns_check() turns down; the pcg_sink_reserve() errors
 */
cerr_check _pcg_columns_emit(const pcg_ctx *ctx, const pcg_column_params *p, const pcg_columns_site *site);

/**
 * define pcg_columns_emit - emit columns at a site described by pcg_columns_site initializers
 *
 * Syntax sugar for _pcg_columns_emit().
 */
#define pcg_columns_emit(_ctx, _p, args...) _pcg_columns_emit((_ctx), (_p), &(const pcg_columns_site){ args })

#endif /* __CLAP_PCG_COLUMN_H__ */
