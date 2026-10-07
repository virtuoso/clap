// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "pcg/column.h"
#include "pcg/internal.h"
#include "pcg/shape.h"

void pcg_column_params_init(pcg_column_params *p)
{
    *p = (pcg_column_params){
        .w          = 0.4f,
        .base_w     = 0.54f,
        .base_h     = 0.45f,
        .top_gap    = 0.02f,
        .shaft      = { .role = PCG_ROLE_CLADDING, .collide = PCG_COLLIDE_SOLID },
        .base       = { .role = PCG_ROLE_STRUCTURE, .collide = PCG_COLLIDE_SOLID },
    };
}

/* the style asks for a base block: a side or a height */
static bool column_has_base(const pcg_column_params *p)
{
    return p->base_w > 0.0f || p->base_h > 0.0f;
}

static cerr column_check(const pcg_column_params *p, float h)
{
    const float dims[] = { p->w, p->base_w, p->base_h, p->top_gap, h };
    if (!pcg_floats_finite(dims, array_size(dims)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "column dimension isn't finite");

    if (!pcg_positive(p->w))    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "column w isn't positive");
    if (!pcg_nonnegative(p->base_w) || !pcg_nonnegative(p->base_h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "column base_w or base_h is negative");
    if (column_has_base(p) && (!pcg_positive(p->base_h) || !(p->w < p->base_w)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "column base without a height, or no wider than the shaft");
    if (!pcg_nonnegative(p->top_gap))   return CERR_INVALID_ARGUMENTS_REASON(.fmt = "column top_gap is negative");

    float shaft_h = h - p->top_gap;
    if (!pcg_positive(shaft_h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "column site h leaves no shaft below the top gap");
    if (column_has_base(p) && !(p->base_h < shaft_h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "column base as tall as the shaft");

    return CERR_OK;
}

/* one checked column at @ctx's frame origin */
static cerr column_emit(const pcg_ctx *ctx, const pcg_column_params *p, float h)
{
    float hw = p->w * 0.5f;

    CERR_RET_CERR(pcg_box(ctx, .lo = { -hw, 0.0f, -hw }, .hi = { hw, h - p->top_gap, hw },
                          .face = PCG_BOX_ALL(&p->shaft)));
    if (!column_has_base(p))    return CERR_OK;

    float hb = p->base_w * 0.5f;

    return pcg_box(ctx, .lo = { -hb, 0.0f, -hb }, .hi = { hb, p->base_h, hb }, .face = PCG_BOX_ALL(&p->base));
}

cerr pcg_column_check(const pcg_column_params *p, const pcg_column_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));

    return column_check(p, site->h);
}

cerr _pcg_column_emit(const pcg_ctx *ctx, const pcg_column_params *p, const pcg_column_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_column_check(p, site));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);

    return column_emit(&at, p, site->h);
}

cerr pcg_columns_check(const pcg_column_params *p, const pcg_columns_site *site)
{
    if (!p || !site)                return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    if (!site->at || !site->nr_at)  return CERR_INVALID_ARGUMENTS_REASON(.fmt = "columns without feet");
    CERR_RET_CERR(column_check(p, site->h));
    for (size_t i = 0; i < site->nr_at; i++)
        if (!isfinite(site->at[i][0]) || !isfinite(site->at[i][1]))
            return CERR_INVALID_ARGUMENTS_REASON(.fmt = "column foot %zu isn't finite", .arg0 = (void *)(uintptr_t)i);

    return CERR_OK;
}

cerr _pcg_columns_emit(const pcg_ctx *ctx, const pcg_column_params *p, const pcg_columns_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_columns_check(p, site));

    pcg_ctx floor = pcg_ctx_at(ctx, &site->frame);

    for (size_t i = 0; i < site->nr_at; i++) {
        const pcg_frame foot = { .org = { site->at[i][0], 0.0f, site->at[i][1] }, .dir = { 1.0f, 0.0f } };
        pcg_ctx at = pcg_ctx_at(&floor, &foot);

        CERR_RET_CERR(column_emit(&at, p, site->h));
    }

    return CERR_OK;
}
