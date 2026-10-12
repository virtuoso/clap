// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "pcg/fixture.h"
#include "pcg/internal.h"
#include "pcg/shape.h"

void pcg_fixture_params_init(pcg_fixture_params *p)
{
    *p = (pcg_fixture_params){
        .r          = 0.16f,
        .h          = 0.05f,
        .drop       = 0.05f,
        .segments   = 12,
        .uv         = PCG_UV_UNIT,
        .body       = { .role = PCG_ROLE_FIXTURE, .collide = PCG_COLLIDE_NONE },
    };
}

cerr pcg_fixture_check(const pcg_fixture_params *p, const pcg_fixture_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    if (!isfinite(p->r) || !isfinite(p->h) || !isfinite(p->drop) || !isfinite(site->ceiling))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fixture radius, thickness, drop or ceiling isn't finite");
    if (!pcg_positive(p->r) || !pcg_positive(p->h) || p->segments < 3)
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fixture without a radius, a thickness or 3 segments");
    if (p->segments > PCG_CYLINDER_SIDES_MAX)
        return CERR_TOO_LARGE_REASON(.fmt = "fixture of more segments than a mesh holds");
    if (!pcg_ordered(p->h, p->drop))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fixture drop less than its thickness");
    if (p->uv >= PCG_UV_NR)     return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fixture UV mode unknown");

    if (!pcg_nonnegative(site->ceiling - p->drop))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fixture ceiling below its drop");

    return CERR_OK;
}

cerr _pcg_fixture_emit(const pcg_ctx *ctx, const pcg_fixture_params *p, const pcg_fixture_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_fixture_check(p, site));

    float base = site->ceiling - p->drop;
    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);

    return pcg_cylinder(&at, .org = { 0.0f, base, 0.0f }, .h = p->h, .r = p->r, .sides = p->segments,
                        .uv = p->uv, .surf = p->body);
}
