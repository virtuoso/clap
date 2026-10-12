/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_GALLERY_H__
#define __CLAP_PCG_GALLERY_H__

#include "error.h"
#include "linmath.h"
#include "pcg/balustrade.h"
#include "pcg/fascia.h"
#include "pcg/sink.h"
#include "pcg/slab.h"
#include "pcg/span.h"

/**
 * struct pcg_gallery_params - a gallery level's style
 * @floor:      the floor slab and its walk finish
 * @fascia:     the drop band around the void
 * @rail:       the balustrade around the void
 * @rail_inset: the rail's line runs this far back from the rim, onto
 *              the floor
 *
 * One mezzanine level around a void: a slab with the void cut out of it,
 * a fascia hanging under the rim, and a rail standing on the finish along
 * the rim. Runs of the fascia and the rail break over cut regions the
 * caller gives, such as where a stair passes; the gallery knows nothing
 * about what makes them.
 */
typedef struct pcg_gallery_params {
    pcg_slab_params         floor;
    pcg_fascia_params       fascia;
    pcg_balustrade_params   rail;
    float                   rail_inset;
} pcg_gallery_params;

/**
 * pcg_gallery_params_init() - a generic gallery's style
 * @p:  the params to fill, every field
 *
 * The children's defaults, from pcg_slab_params_init(),
 * pcg_fascia_params_init() and pcg_balustrade_params_init(), with a
 * 0.35 m slab for the span to the perimeter, and the rail 0.09 back from
 * the rim.
 */
void pcg_gallery_params_init(pcg_gallery_params *p);

/**
 * struct pcg_gallery_site - where a gallery level goes
 * @frame:          the level's frame; its y is the top of the slab's
 *                  structure, with the finish laid on it
 * @outline:        the floor's outline in the frame's (a, c)
 * @nr_outline:     number of points
 * @rim:            the void's outline in the frame's (a, c), wound
 *                  counterclockwise, so the void is to the left of every
 *                  edge; inside @outline
 * @nr_rim:         number of points
 * @rail_cuts:      regions of the frame's (a, c) where the rail stays
 *                  open; nullptr: none
 * @nr_rail_cuts:   number of @rail_cuts
 * @fascia_cuts:    regions of the frame's (a, c) the fascia breaks over;
 *                  nullptr: none
 * @nr_fascia_cuts: number of @fascia_cuts
 *
 * The slab's hole is @rim. The fascia hangs from the top of the
 * structure along @rim, the rail stands on the finish @rail_inset back
 * from @rim, and both chart from their own floor, so levels at different
 * heights wear their texture alike.
 */
typedef struct pcg_gallery_site {
    pcg_frame           frame;
    const vec2          *outline;
    size_t              nr_outline;
    const vec2          *rim;
    size_t              nr_rim;
    const pcg_region    *rail_cuts;
    size_t              nr_rail_cuts;
    const pcg_region    *fascia_cuts;
    size_t              nr_fascia_cuts;
} pcg_gallery_site;

/**
 * pcg_gallery_check() - check a gallery level's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Everything _pcg_gallery_emit() checks before it emits anything: the
 * slab's, the fascia's and the rail's own checks, at the sites the level
 * gives them, and the rim's winding. A parent that places a level among
 * other children makes the checks before any of its children emits, so
 * that a bad level leaves nothing behind. The outline and the rim are
 * pcg_plate()'s to triangulate, when the slab is emitted.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, an
 * outline of fewer than 3 points, missing, with a point that isn't finite
 * or an edge without length, what pcg_slab_check() turns
 * down for the outline, pcg_fascia_check() for the fascia along the rim
 * and pcg_balustrade_path_check() for the rail along it, and a rim that
 * doesn't wind counterclockwise
 */
cerr_check pcg_gallery_check(const pcg_gallery_params *p, const pcg_gallery_site *site);

/**
 * _pcg_gallery_emit() - emit a gallery level
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Checks everything pcg_gallery_check() does before anything is emitted.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; CERR_NOMEM; the
 * pcg_gallery_check() errors; the pcg_slab_emit(), pcg_fascia_emit() and
 * pcg_balustrade_path_emit() errors
 */
cerr_check _pcg_gallery_emit(const pcg_ctx *ctx, const pcg_gallery_params *p, const pcg_gallery_site *site);

/**
 * define pcg_gallery_emit - emit a gallery level at a site described by pcg_gallery_site initializers
 *
 * Syntax sugar for _pcg_gallery_emit().
 */
#define pcg_gallery_emit(_ctx, _p, args...) _pcg_gallery_emit((_ctx), (_p), &(const pcg_gallery_site){ args })

#endif /* __CLAP_PCG_GALLERY_H__ */
