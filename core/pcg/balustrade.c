// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <math.h>
#include "memory.h"
#include "pcg/balustrade.h"
#include "pcg/internal.h"
#include "pcg/shape.h"

/* a sloped run's shoe and cap are square rods, turned half a side so that a flat faces up */
#define BALUSTRADE_ROD_SIDES 4

void pcg_balustrade_params_init(pcg_balustrade_params *p)
{
    const pcg_surface rail = { .role = PCG_ROLE_RAIL, .collide = PCG_COLLIDE_HAZARD };

    *p = (pcg_balustrade_params){
        .h              = 1.05f,
        .cap_t          = 0.07f,
        .rail_w         = 0.10f,
        .shoe_h         = 0.10f,
        .shoe_w         = 0.081f,
        .slat_w         = 0.09f,
        .slat_t         = 0.045f,
        .gap            = 0.05f,
        .slat_overlap   = 0.02f,
        .slope_shoe_r   = 0.07f,
        .slope_cap_r    = 0.06f,
        .min_run        = 0.15f,
        .merge          = 0.05f,
        .min_frac       = 1e-3f,
        .cap            = rail,
        .shoe           = rail,
        .infill         = rail,
    };
}

cerr pcg_balustrade_params_check(const pcg_balustrade_params *p)
{
    if (!p)     return CERR_INVALID_ARGUMENTS;

    const float dims[] = {
        p->h, p->cap_t, p->rail_w, p->shoe_h, p->shoe_w, p->slat_w, p->slat_t, p->gap, p->slat_overlap,
        p->slope_shoe_r, p->slope_cap_r, p->min_run, p->merge,
    };
    if (!pcg_floats_finite(dims, array_size(dims)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "balustrade dimension isn't finite");

    if (!pcg_positive(p->h) || !pcg_positive(p->cap_t) || !pcg_positive(p->rail_w) ||
        !pcg_positive(p->shoe_h) || !pcg_positive(p->shoe_w))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "balustrade cap or shoe dimension isn't positive");
    if (!pcg_positive(p->slat_w) || !pcg_positive(p->slat_t) || !pcg_nonnegative(p->gap) ||
        !pcg_nonnegative(p->slat_overlap))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "balustrade slat dimension isn't positive, or gap negative");
    if (!pcg_positive(p->slope_shoe_r) || !pcg_positive(p->slope_cap_r))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "balustrade sloped rod radius isn't positive");
    if (!(p->shoe_h < p->h - p->cap_t))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "balustrade slats have no height between shoe and cap");
    if (!pcg_nonnegative(p->min_run) || !pcg_nonnegative(p->merge))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "balustrade min_run or merge is negative");
    if (!pcg_nonnegative(p->min_frac) || !(p->min_frac < 1.0f))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "balustrade min_frac outside [0, 1)");

    return CERR_OK;
}

/* one checked run along +a of @ctx's frame, from its origin, standing on @y0 */
static cerr balustrade_run(const pcg_ctx *ctx, const pcg_balustrade_params *p, float len, float rise, float y0)
{
    float nf = roundf(len / (p->slat_w + p->gap));
    if (!(nf < (float)INT_MAX))     return CERR_TOO_LARGE_REASON(.fmt = "balustrade run of too many slats");

    if (rise == 0.0f) {
        float sw = p->shoe_w * 0.5f, rw = p->rail_w * 0.5f;

        CERR_RET_CERR(pcg_box(ctx, .lo = { 0.0f, y0, -sw }, .hi = { len, y0 + p->shoe_h, sw },
                              .face = PCG_BOX_ALL(&p->shoe)));
        CERR_RET_CERR(pcg_box(ctx, .lo = { 0.0f, y0 + p->h - p->cap_t, -rw }, .hi = { len, y0 + p->h, rw },
                              .face = PCG_BOX_ALL(&p->cap)));
    } else {
        const float phase = (float)M_PI / BALUSTRADE_ROD_SIDES;

        CERR_RET_CERR(pcg_rod(ctx, .a = { 0.0f, y0 + p->shoe_h * 0.5f, 0.0f },
                              .b = { len, y0 + rise + p->shoe_h * 0.5f, 0.0f },
                              .ra = p->slope_shoe_r, .rb = p->slope_shoe_r, .phase = phase,
                              .sides = BALUSTRADE_ROD_SIDES, .surf = p->shoe));
        CERR_RET_CERR(pcg_rod(ctx, .a = { 0.0f, y0 + p->h - p->cap_t * 0.5f, 0.0f },
                              .b = { len, y0 + rise + p->h - p->cap_t * 0.5f, 0.0f },
                              .ra = p->slope_cap_r, .rb = p->slope_cap_r, .phase = phase,
                              .sides = BALUSTRADE_ROD_SIDES, .surf = p->cap));
    }

    int n = nf < 1.0f ? 1 : (int)nf;
    /* rounded up, the count may be one more than fits side by side */
    if (n > 1 && n * p->slat_w > len)   n = max((int)(len / p->slat_w), 1);

    float pitch = len / n, hw = p->slat_w * 0.5f, ht = p->slat_t * 0.5f;

    for (int i = 0; i < n; i++) {
        float at = (i + 0.5f) * pitch;
        float base = y0 + rise * at / len;

        CERR_RET_CERR(pcg_box(ctx, .lo = { at - hw, base + p->shoe_h - p->slat_overlap, -ht },
                              .hi = { at + hw, base + p->h - p->cap_t + p->slat_overlap, ht },
                              .face = PCG_BOX_ALL(&p->infill)));
    }

    return CERR_OK;
}

cerr pcg_balustrade_check(const pcg_balustrade_params *p, const pcg_balustrade_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_balustrade_params_check(p));
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    if (!pcg_positive_finite(site->len) || !isfinite(site->rise) || !isfinite(site->y))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "balustrade run without length, or rise or y not finite");

    return CERR_OK;
}

cerr _pcg_balustrade_emit(const pcg_ctx *ctx, const pcg_balustrade_params *p, const pcg_balustrade_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_balustrade_check(p, site));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);

    return balustrade_run(&at, p, site->len, site->rise, site->y);
}

cerr pcg_balustrade_path_check(const pcg_balustrade_params *p, const pcg_balustrade_path *path)
{
    if (!p || !path)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_balustrade_params_check(p));
    CERR_RET_CERR(pcg_frame_check(&path->frame));
    CERR_RET_CERR(pcg_path_check(path->pts, path->nr_pts, path->closed, "balustrade path"));
    if (path->nr_cuts && !path->cuts)   return CERR_INVALID_ARGUMENTS_REASON(.fmt = "balustrade path cuts missing");
    if (!isfinite(path->y) || !isfinite(path->inset))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "balustrade path y or inset not finite");

    return pcg_regions_check(path->cuts, path->nr_cuts);
}

cerr _pcg_balustrade_path_emit(const pcg_ctx *ctx, const pcg_balustrade_params *p,
                               const pcg_balustrade_path *path)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_balustrade_path_check(p, path));

    size_t cap = path->nr_cuts + 1;
    LOCAL_SET(void, buf) = mem_alloc(sizeof(pcg_span), .nr = cap);
    if (!buf)   return CERR_NOMEM;

    pcg_span *runs = buf;
    pcg_ctx at = pcg_ctx_at(ctx, &path->frame);
    const pcg_span_tol tol = { .min_frac = p->min_frac, .merge = p->merge, .min_run = p->min_run };
    size_t nr_edges = path->closed ? path->nr_pts : path->nr_pts - 1;

    for (size_t i = 0; i < nr_edges; i++) {
        const float *a = path->pts[i], *b = path->pts[(i + 1) % path->nr_pts];
        pcg_frame edge;

        CRES_RET_CERR(pcg_frame_run(&edge, a, b, 0.0f));
        size_t nr = CRES_RET_CERR(pcg_segment_runs(a, b, path->cuts, path->nr_cuts, &tol, runs, cap));
        pcg_ctx ec = pcg_ctx_at(&at, &edge);

        for (size_t k = 0; k < nr; k++) {
            const pcg_frame run = { .org = { runs[k].lo, 0.0f, -path->inset }, .dir = { 1.0f, 0.0f } };
            pcg_ctx rc = pcg_ctx_at(&ec, &run);

            CERR_RET_CERR(balustrade_run(&rc, p, runs[k].hi - runs[k].lo, 0.0f, path->y));
        }
    }

    return CERR_OK;
}
