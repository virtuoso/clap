// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "pcg/glazing.h"
#include "pcg/internal.h"
#include "pcg/shape.h"

void pcg_glazing_params_init(pcg_glazing_params *p)
{
    *p = (pcg_glazing_params){
        .member_w   = 0.06f,
        .member_t   = 0.05f,
        .min_seg    = 0.01f,
        .sill       = true,
        .pane       = { .role = PCG_ROLE_GLASS, .collide = PCG_COLLIDE_SOLID },
        .member     = { .role = PCG_ROLE_METAL, .collide = PCG_COLLIDE_NONE },
    };
}

static cerr glazing_check(const pcg_glazing_params *p, const pcg_glazing_site *site)
{
    const float dims[] = { p->member_w, p->member_t, p->min_seg, site->len, site->y0, site->y1 };
    if (!pcg_floats_finite(dims, array_size(dims)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "glazing dimension isn't finite");

    if (!pcg_positive(p->member_w) || !pcg_positive(p->member_t) || !pcg_nonnegative(p->min_seg))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "glazing member dimension isn't positive, or min_seg negative");
    if (!pcg_positive(site->len) || !(site->y0 < site->y1))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "glazing pane without length or height");
    CERR_RET_CERR(pcg_openings_check(site->openings, site->nr_openings, site->len));

    for (size_t i = 0; i < site->nr_openings; i++)
        if (!isfinite(site->openings[i].h))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "glazing opening %zu has a head that isn't finite",
                .arg0   = (void *)(uintptr_t)i
            );

    return CERR_OK;
}

/* an opening with a head member over it, under a transom: pcg_pane()'s test for one */
static bool glazing_headed(const pcg_glazing_site *site, const pcg_opening *o)
{
    return o->h > site->y0 && o->h < site->y1;
}

/* the members fit the pane, and the jambs and heads of its openings fit between them */
static cerr glazing_members_check(const pcg_glazing_params *p, const pcg_glazing_site *site)
{
    float fw = p->member_w;

    if (!(2.0f * fw < site->len) || !(2.0f * fw < site->y1 - site->y0))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "glazing pane less than two members long or high");

    /* the inner edge of the last member along the pane */
    float edge = fw;

    for (size_t i = 0; i < site->nr_openings; i++) {
        const pcg_opening *o = &site->openings[i];
        pcg_span s = pcg_opening_span(o);

        if (!pcg_ordered(edge, s.lo - fw) || !pcg_ordered(s.hi + fw, site->len - fw))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "glazing opening %zu has no room for its jambs",
                .arg0   = (void *)(uintptr_t)i
            );
        if (glazing_headed(site, o) && !pcg_ordered(o->h + fw, site->y1 - fw))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "glazing opening %zu has no room for its head member",
                .arg0   = (void *)(uintptr_t)i
            );
        edge = s.hi + fw;
    }

    return CERR_OK;
}

/* one member over [@a0, @a1] x [@y0, @y1], centered on the pane */
static cerr glazing_member(const pcg_ctx *ctx, const pcg_glazing_params *p, float a0, float a1, float y0, float y1)
{
    float hft = p->member_t * 0.5f;

    return pcg_box(ctx, .lo = { a0, y0, -hft }, .hi = { a1, y1, hft }, .face = PCG_BOX_ALL(&p->member));
}

/*
 * A horizontal member over [@y0, @y1] between the end verticals, broken
 * around the openings and their jambs: around every opening with @all,
 * otherwise around those without a head member
 */
static cerr glazing_rail(const pcg_ctx *ctx, const pcg_glazing_params *p, const pcg_glazing_site *site, bool all,
                         float y0, float y1)
{
    float fw = p->member_w, at = fw;

    for (size_t i = 0; i <= site->nr_openings; i++) {
        pcg_span gap = { .lo = site->len - fw };

        if (i < site->nr_openings) {
            const pcg_opening *o = &site->openings[i];
            pcg_span s = pcg_opening_span(o);

            if (!all && glazing_headed(site, o))    continue;
            gap = (pcg_span){ .lo = s.lo - fw, .hi = s.hi + fw };
        }

        float w = gap.lo - at;
        if (w > 0.0f && w >= p->min_seg)    CERR_RET_CERR(glazing_member(ctx, p, at, gap.lo, y0, y1));
        at = gap.hi;
    }

    return CERR_OK;
}

cerr pcg_glazing_check(const pcg_glazing_params *p, const pcg_glazing_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    CERR_RET_CERR(glazing_check(p, site));
    if (!p->member.skip)    CERR_RET_CERR(glazing_members_check(p, site));

    return CERR_OK;
}

cerr _pcg_glazing_emit(const pcg_ctx *ctx, const pcg_glazing_params *p, const pcg_glazing_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_glazing_check(p, site));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    float fw = p->member_w, len = site->len, y0 = site->y0, y1 = site->y1;

    CERR_RET_CERR(pcg_pane(&at, .open = site->openings, .nr_open = site->nr_openings, .len = len, .y0 = y0,
                           .y1 = y1, .min_seg = p->min_seg, .surf = p->pane));
    if (p->member.skip)     return CERR_OK;

    /* the verticals at either end, the full height, and the horizontals between them */
    CERR_RET_CERR(glazing_member(&at, p, 0.0f, fw, y0, y1));
    CERR_RET_CERR(glazing_member(&at, p, len - fw, len, y0, y1));
    CERR_RET_CERR(glazing_rail(&at, p, site, false, y1 - fw, y1));
    if (p->sill)    CERR_RET_CERR(glazing_rail(&at, p, site, true, y0, y0 + fw));

    /* a jamb either side of every opening, up to its head member or the top */
    for (size_t i = 0; i < site->nr_openings; i++) {
        const pcg_opening *o = &site->openings[i];
        pcg_span s = pcg_opening_span(o);
        bool headed = glazing_headed(site, o);
        float top = headed ? o->h : y1;

        CERR_RET_CERR(glazing_member(&at, p, s.lo - fw, s.lo, y0, top));
        CERR_RET_CERR(glazing_member(&at, p, s.hi, s.hi + fw, y0, top));
        if (headed)     CERR_RET_CERR(glazing_member(&at, p, s.lo - fw, s.hi + fw, o->h, o->h + fw));
    }

    return CERR_OK;
}
