// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <math.h>
#include "pcg/internal.h"
#include "pcg/shape.h"
#include "pcg/window-wall.h"

void pcg_window_wall_params_init(pcg_window_wall_params *p)
{
    const pcg_surface cladding = { .role = PCG_ROLE_CLADDING, .collide = PCG_COLLIDE_SOLID };

    *p = (pcg_window_wall_params){
        .t          = 0.22f,
        .sill       = 0.95f,
        .head       = 3.05f,
        .bay        = 4.8f,
        .post_w     = 0.3f,
        .mull_w     = 0.09f,
        .mull_pitch = 1.2f,
        .open_below = false,
        .spandrel   = cladding,
        .header     = cladding,
        .post       = cladding,
        .mullion    = { .role = PCG_ROLE_METAL, .collide = PCG_COLLIDE_SOLID },
    };

    pcg_glazing_params_init(&p->glazing);
    p->glazing.member.skip = true;
}

static cerr window_wall_check(const pcg_window_wall_params *p, const pcg_window_wall_site *site)
{
    const float dims[] = { p->t, p->sill, p->head, p->bay, p->post_w, p->mull_w, p->mull_pitch, site->len, site->h };
    if (!pcg_floats_finite(dims, array_size(dims)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "window wall dimension isn't finite");

    if (!pcg_positive(p->t) || !pcg_positive(p->sill) || !pcg_positive(p->bay) || !pcg_positive(p->post_w) ||
        !pcg_positive(p->mull_w) || !pcg_positive(p->mull_pitch))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "window wall dimension isn't positive");
    if (!(p->sill < p->head) || !(p->head < site->h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "window wall head not above the sill, or not below the top");
    if (!pcg_positive(site->len) || !(2.0f * p->post_w <= site->len))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "window wall shorter than its end posts");
    CERR_RET_CERR(pcg_openings_check(site->openings, site->nr_openings, site->len));

    /* the inner edge of the last post along the wall */
    float edge = p->post_w, lo = p->open_below ? 0.0f : p->sill;

    for (size_t i = 0; i < site->nr_openings; i++) {
        const pcg_opening *o = &site->openings[i];
        pcg_span s = pcg_opening_span(o);

        if (!pcg_ordered(edge, s.lo - p->post_w) || !pcg_ordered(s.hi + p->post_w, site->len - p->post_w))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "window wall opening %zu has no room for its posts",
                .arg0   = (void *)(uintptr_t)i
            );
        if (!(o->h > lo) || !(o->h <= p->head))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "window wall opening %zu has its head outside the window band",
                .arg0   = (void *)(uintptr_t)i
            );
        edge = s.hi + p->post_w;
    }

    return CERR_OK;
}

/* @at is closer than @margin to an opening, or inside one */
static bool window_wall_in_opening(const pcg_window_wall_site *site, float at, float margin)
{
    for (size_t i = 0; i < site->nr_openings; i++) {
        pcg_span s = pcg_opening_span(&site->openings[i]);

        if (at > s.lo - margin && at < s.hi + margin)   return true;
    }

    return false;
}

/* one solid piece over [@a0, @a1] x [@y0, @y1], centered on the wall's line */
static cerr window_wall_box(const pcg_ctx *ctx, const pcg_window_wall_params *p, const pcg_surface *surf,
                            float a0, float a1, float y0, float y1)
{
    float ht = p->t * 0.5f;

    return pcg_box(ctx, .lo = { a0, y0, -ht }, .hi = { a1, y1, ht }, .face = PCG_BOX_ALL(surf));
}

/*
 * struct window_wall_fit - how a window wall divides its length
 * @bays:   bay count
 * @k:      panes per bay
 * @bay:    bay width
 */
typedef struct window_wall_fit {
    int     bays;
    int     k;
    float   bay;
} window_wall_fit;

/* the glazing over the window band of a wall of style @p at @site */
static pcg_glazing_site window_wall_glazing(const pcg_window_wall_params *p, const pcg_window_wall_site *site)
{
    return (pcg_glazing_site){
        .frame          = PCG_FRAME_IDENTITY,
        .len            = site->len,
        .y0             = p->open_below ? 0.0f : p->sill,
        .y1             = p->head,
        .openings       = site->openings,
        .nr_openings    = site->nr_openings,
    };
}

/* check @p and @site, the glazing's included, and divide the wall into bays and panes */
static cerr window_wall_fit_get(const pcg_window_wall_params *p, const pcg_window_wall_site *site,
                                window_wall_fit *fit)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    CERR_RET_CERR(window_wall_check(p, site));

    float len = site->len, pw = p->post_w, mw = p->mull_w;

    float nr_bays = len / p->bay;
    if (!(nr_bays < (float)INT_MAX))    return CERR_TOO_LARGE_REASON(.fmt = "window wall of too many bays");
    int bays = (int)roundf(nr_bays);
    if (bays < 1)   bays = 1;

    float bay = len / bays, nr_panes = roundf(bay / p->mull_pitch);
    if (!(nr_panes < (float)INT_MAX))   return CERR_TOO_LARGE_REASON(.fmt = "window wall bay of too many panes");
    int k = (int)nr_panes;
    if (k < 1)  k = 1;

    /*
     * Glass between every two members: the narrowest panes are the first
     * and the last, against an end post, and those between mullions
     */
    float end_pane = (k > 1 ? bay / k - mw * 0.5f : bays > 1 ? bay - pw * 0.5f : len - pw) - pw;
    float mid_pane = k > 1 ? bay / k - mw : end_pane;
    if (!(end_pane > 0.0f) || !(mid_pane > 0.0f))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "window wall bays or panes no wider than their posts and mullions");

    pcg_glazing_site glazing = window_wall_glazing(p, site);
    CERR_RET_CERR(pcg_glazing_check(&p->glazing, &glazing));

    *fit = (window_wall_fit){ .bays = bays, .k = k, .bay = bay };

    return CERR_OK;
}

cerr pcg_window_wall_check(const pcg_window_wall_params *p, const pcg_window_wall_site *site)
{
    window_wall_fit fit;

    return window_wall_fit_get(p, site, &fit);
}

cerr _pcg_window_wall_emit(const pcg_ctx *ctx, const pcg_window_wall_params *p, const pcg_window_wall_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;

    window_wall_fit fit;
    CERR_RET_CERR(window_wall_fit_get(p, site, &fit));

    float len = site->len, h = site->h, head = p->head, pw = p->post_w, mw = p->mull_w;
    float lo = p->open_below ? 0.0f : p->sill, bay = fit.bay;
    int bays = fit.bays, k = fit.k;

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);

    /* the band's glass */
    pcg_glazing_site glazing = window_wall_glazing(p, site);
    CERR_RET_CERR(_pcg_glazing_emit(&at, &p->glazing, &glazing));

    /* the spandrel between the openings, and the header over everything */
    if (!p->open_below) {
        float run = 0.0f;

        for (size_t i = 0; i <= site->nr_openings; i++) {
            pcg_span s = i < site->nr_openings ? pcg_opening_span(&site->openings[i]) : (pcg_span){ .lo = len };

            if (s.lo > run)     CERR_RET_CERR(window_wall_box(&at, p, &p->spandrel, run, s.lo, 0.0f, p->sill));
            run = s.hi;
        }
    }
    CERR_RET_CERR(window_wall_box(&at, p, &p->header, 0.0f, len, head, h));

    /* end posts, posts either side of the openings from the floor, bay posts */
    CERR_RET_CERR(window_wall_box(&at, p, &p->post, 0.0f, pw, lo, head));
    CERR_RET_CERR(window_wall_box(&at, p, &p->post, len - pw, len, lo, head));
    for (size_t i = 0; i < site->nr_openings; i++) {
        pcg_span s = pcg_opening_span(&site->openings[i]);

        CERR_RET_CERR(window_wall_box(&at, p, &p->post, s.lo - pw, s.lo, 0.0f, head));
        CERR_RET_CERR(window_wall_box(&at, p, &p->post, s.hi, s.hi + pw, 0.0f, head));
    }

    /* bay posts and mullions that would touch an opening's posts give way to them */
    for (int b = 1; b < bays; b++) {
        float a = b * bay;

        if (window_wall_in_opening(site, a, pw * 1.5f))     continue;
        CERR_RET_CERR(window_wall_box(&at, p, &p->post, a - pw * 0.5f, a + pw * 0.5f, lo, head));
    }

    /* mullions inside each bay */
    for (int b = 0; b < bays; b++)
        for (int i = 1; i < k; i++) {
            float a = b * bay + i * bay / k;

            if (window_wall_in_opening(site, a, pw + mw * 0.5f))    continue;
            CERR_RET_CERR(window_wall_box(&at, p, &p->mullion, a - mw * 0.5f, a + mw * 0.5f, lo, head));
        }

    return CERR_OK;
}
