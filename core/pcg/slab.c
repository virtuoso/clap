// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "pcg/internal.h"
#include "pcg/shape.h"
#include "pcg/slab.h"

void pcg_slab_params_init(pcg_slab_params *p)
{
    const pcg_surface structure = { .role = PCG_ROLE_STRUCTURE, .collide = PCG_COLLIDE_SOLID };
    const pcg_surface floor = { .role = PCG_ROLE_FLOOR, .collide = PCG_COLLIDE_SOLID };

    *p = (pcg_slab_params){
        .t              = 0.3f,
        .finish_t       = 0.05f,
        .top            = structure,
        .soffit         = structure,
        .edge           = structure,
        .finish         = floor,
        .finish_edge    = floor,
    };
}

cerr pcg_slab_check(const pcg_slab_params *p, const pcg_slab_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    if (!pcg_positive_finite(p->t)) return CERR_INVALID_ARGUMENTS_REASON(.fmt = "slab t isn't positive and finite");
    if (!pcg_nonnegative_finite(p->finish_t))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "slab finish_t is negative or isn't finite");

    /* pcg_plate() turns down a missing outline */
    for (size_t i = 0; site->pts && i < site->nr_pts; i++)
        if (!isfinite(site->pts[i][0]) || !isfinite(site->pts[i][1]))
            return CERR_INVALID_ARGUMENTS_REASON(.fmt = "slab point %zu isn't finite", .arg0 = (void *)(uintptr_t)i);

    return CERR_OK;
}

cerr _pcg_slab_emit(const pcg_ctx *ctx, const pcg_slab_params *p, const pcg_slab_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_slab_check(p, site));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    bool finished = p->finish_t > 0.0f;
    const pcg_surface skip = { .skip = true };

    /* under a finish, the structure's top is buried */
    CERR_RET_CERR(pcg_plate(&at, .pts = site->pts, .nr_pts = site->nr_pts, .rings = site->rings,
                            .nr_rings = site->nr_rings, .y0 = -p->t, .y1 = 0.0f,
                            .top = finished ? skip : p->top, .bottom = p->soffit, .edge = p->edge));
    if (!finished)  return CERR_OK;

    return pcg_plate(&at, .pts = site->pts, .nr_pts = site->nr_pts, .rings = site->rings,
                     .nr_rings = site->nr_rings, .y0 = 0.0f, .y1 = p->finish_t,
                     .top = p->finish, .bottom = skip, .edge = p->finish_edge);
}
