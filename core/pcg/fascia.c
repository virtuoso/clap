// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "memory.h"
#include "pcg/fascia.h"
#include "pcg/internal.h"
#include "pcg/shape.h"

void pcg_fascia_params_init(pcg_fascia_params *p)
{
    const pcg_surface structure = { .role = PCG_ROLE_STRUCTURE, .collide = PCG_COLLIDE_SOLID };

    *p = (pcg_fascia_params){
        .depth      = 0.9f,
        .lip        = 0.06f,
        .tuck       = 0.02f,
        .min_seg    = 0.05f,
        .min_frac   = 1e-3f,
        .face       = structure,
        .back       = structure,
    };
}

cerr pcg_fascia_params_check(const pcg_fascia_params *p)
{
    if (!p)     return CERR_INVALID_ARGUMENTS;

    const float dims[] = { p->depth, p->lip, p->tuck, p->min_seg, p->min_frac };
    if (!pcg_floats_finite(dims, array_size(dims)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fascia dimension isn't finite");

    if (!pcg_positive(p->depth))        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fascia depth isn't positive");
    if (!pcg_nonnegative(p->lip) || !pcg_nonnegative(p->tuck) || !pcg_positive(p->lip + p->tuck))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fascia lip and tuck negative, or no width");
    if (!pcg_nonnegative(p->min_seg))   return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fascia min_seg is negative");
    if (!pcg_nonnegative(p->min_frac) || !(p->min_frac < 1.0f))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fascia min_frac outside [0, 1)");

    return CERR_OK;
}

cerr pcg_fascia_check(const pcg_fascia_params *p, const pcg_fascia_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_fascia_params_check(p));
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    CERR_RET_CERR(pcg_path_check(site->pts, site->nr_pts, site->closed, "fascia path"));
    if (site->nr_cuts && !site->cuts)   return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fascia cuts missing");

    return pcg_regions_check(site->cuts, site->nr_cuts);
}

cerr _pcg_fascia_emit(const pcg_ctx *ctx, const pcg_fascia_params *p, const pcg_fascia_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_fascia_check(p, site));

    size_t cap = site->nr_cuts + 1;
    LOCAL_SET(void, buf) = mem_alloc(sizeof(pcg_span), .nr = cap);
    if (!buf)   return CERR_NOMEM;

    pcg_span *runs = buf;
    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    /* a piece between cuts closer than min_seg is no longer than it either */
    const pcg_span_tol tol = { .min_frac = p->min_frac, .merge = p->min_seg, .min_run = p->min_seg };
    size_t nr_edges = site->closed ? site->nr_pts : site->nr_pts - 1;

    for (size_t i = 0; i < nr_edges; i++) {
        const float *a = site->pts[i], *b = site->pts[(i + 1) % site->nr_pts];
        pcg_frame edge;

        CRES_RET_CERR(pcg_frame_run(&edge, a, b, 0.0f));
        size_t nr = CRES_RET_CERR(pcg_segment_runs(a, b, site->cuts, site->nr_cuts, &tol, runs, cap));
        pcg_ctx ec = pcg_ctx_at(&at, &edge);

        for (size_t k = 0; k < nr; k++) {
            const vec3 lo = { runs[k].lo, -p->depth, -p->tuck }, hi = { runs[k].hi, 0.0f, p->lip };

            CERR_RET_CERR(pcg_band(&ec, .lo = { lo[0], lo[1], lo[2] }, .hi = { hi[0], hi[1], hi[2] },
                                   .surf = p->face, .skip_mask = PRIM_BOX_NEG_C));
            CERR_RET_CERR(pcg_band(&ec, .lo = { lo[0], lo[1], lo[2] }, .hi = { hi[0], hi[1], hi[2] },
                                   .surf = p->back, .skip_mask = ~(unsigned int)PRIM_BOX_NEG_C));
        }
    }

    return CERR_OK;
}
