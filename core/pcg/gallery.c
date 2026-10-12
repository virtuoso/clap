// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "memory.h"
#include "poly2d.h"
#include "triangulate.h"
#include "pcg/gallery.h"
#include "pcg/internal.h"

void pcg_gallery_params_init(pcg_gallery_params *p)
{
    *p = (pcg_gallery_params){ .rail_inset = 0.09f };
    pcg_slab_params_init(&p->floor);
    p->floor.t = 0.35f;
    pcg_fascia_params_init(&p->fascia);
    pcg_balustrade_params_init(&p->rail);
}

/* the fascia that hangs along @site's rim */
static pcg_fascia_site gallery_fascia(const pcg_gallery_site *site)
{
    return (pcg_fascia_site){
        .frame      = site->frame,
        .pts        = site->rim,
        .nr_pts     = site->nr_rim,
        .cuts       = site->fascia_cuts,
        .nr_cuts    = site->nr_fascia_cuts,
        .closed     = true,
    };
}

/* the rail along @site's rim: it stands on the finish, and charts from there */
static pcg_balustrade_path gallery_rail(const pcg_gallery_params *p, const pcg_gallery_site *site)
{
    pcg_balustrade_path rail = {
        .frame      = site->frame,
        .pts        = site->rim,
        .nr_pts     = site->nr_rim,
        .cuts       = site->rail_cuts,
        .nr_cuts    = site->nr_rail_cuts,
        .inset      = p->rail_inset,
        .closed     = true,
    };

    rail.frame.org[1] += p->floor.finish_t;

    return rail;
}

cerr pcg_gallery_check(const pcg_gallery_params *p, const pcg_gallery_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_path_check(site->outline, site->nr_outline, true, "gallery outline"));
    CERR_RET_CERR(pcg_slab_check(&p->floor, &(const pcg_slab_site){ .frame = site->frame, .pts = site->outline,
                                                                   .nr_pts = site->nr_outline }));

    const pcg_fascia_site fascia = gallery_fascia(site);
    CERR_RET_CERR(pcg_fascia_check(&p->fascia, &fascia));
    const pcg_balustrade_path rail = gallery_rail(p, site);
    CERR_RET_CERR(pcg_balustrade_path_check(&p->rail, &rail));

    /* the slab takes the rim either way round, the fascia and the rail only with the void to their left */
    if (!(poly_ring_area(site->rim, &(const poly_ring){ .start = 0, .nr = site->nr_rim }) > 0.0f))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "gallery rim isn't counterclockwise");

    return CERR_OK;
}

cerr _pcg_gallery_emit(const pcg_ctx *ctx, const pcg_gallery_params *p, const pcg_gallery_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_gallery_check(p, site));

    size_t no = site->nr_outline, nh = site->nr_rim;
    LOCAL_SET(void, buf) = mem_alloc(sizeof(vec2), .nr = no + nh);
    if (!buf)   return CERR_NOMEM;

    /* the slab triangulates the outline around the rim as its hole, whichever way they wind */
    vec2 *pts = buf;
    memcpy(pts, site->outline, no * sizeof(vec2));
    memcpy(pts + no, site->rim, nh * sizeof(vec2));
    const poly_ring rings[2] = { { .start = 0, .nr = no }, { .start = no, .nr = nh } };

    CERR_RET_CERR(pcg_slab_emit(ctx, &p->floor, .frame = site->frame, .pts = pts, .nr_pts = no + nh,
                                .rings = rings, .nr_rings = 2));

    const pcg_fascia_site fascia = gallery_fascia(site);
    CERR_RET_CERR(_pcg_fascia_emit(ctx, &p->fascia, &fascia));
    const pcg_balustrade_path rail = gallery_rail(p, site);

    return _pcg_balustrade_path_emit(ctx, &p->rail, &rail);
}
