// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <math.h>
#include "memory.h"
#include "poly2d.h"
#include "pcg/internal.h"
#include "pcg/shape.h"
#include "pcg/skylight.h"

void pcg_skylight_params_init(pcg_skylight_params *p)
{
    *p = (pcg_skylight_params){
        .curb_w     = 0.25f,
        .curb_h     = 0.5f,
        .beam_w     = 0.07f,
        .beam_h     = 0.4f,
        .pitch      = 1.2f,
        .embed      = 0.08f,
        .cross_drop = 0.03f,
        .lift       = 0.12f,
        .frame      = { .role = PCG_ROLE_METAL, .collide = PCG_COLLIDE_SOLID },
        .diffuser   = { .role = PCG_ROLE_DIFFUSER, .collide = PCG_COLLIDE_NONE },
    };
}

/* the style and the site, before the aperture is known */
static cerr skylight_site_check(const pcg_skylight_params *p, const pcg_skylight_site *site)
{
    CERR_RET_CERR(pcg_frame_check(&site->frame));

    const float dims[] = {
        p->curb_w, p->curb_h, p->beam_w, p->beam_h, p->pitch, p->embed, p->cross_drop, p->lift, site->r,
    };
    if (!pcg_floats_finite(dims, array_size(dims)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "skylight dimension isn't finite");

    if (!pcg_positive(p->curb_w) || !pcg_positive(p->curb_h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "skylight curb_w or curb_h isn't positive");
    if (!pcg_positive(p->beam_w) || !pcg_positive(p->beam_h) || !pcg_positive(p->pitch))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "skylight beam_w, beam_h or pitch isn't positive");
    if (!pcg_nonnegative(p->embed) || !pcg_ordered(p->embed, p->curb_w))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "skylight embed outside the curb");
    if (!pcg_nonnegative(p->cross_drop) || !(p->cross_drop < p->beam_h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "skylight cross_drop negative or as deep as a beam");
    if (!pcg_nonnegative(p->lift))  return CERR_INVALID_ARGUMENTS_REASON(.fmt = "skylight lift is negative");

    if (site->sides < 3 || !pcg_positive(site->r))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "skylight aperture of fewer than 3 sides or without a radius");
    if (site->sides > PCG_SKYLIGHT_SIDES_MAX)
        return CERR_TOO_LARGE_REASON(.fmt = "skylight aperture of more sides than a mesh holds");

    return CERR_OK;
}

/* the number of bays across @extent, nearest to @pitch wide each */
static cres(int) skylight_bays(float extent, float pitch)
{
    float bays = roundf(extent / pitch);

    if (!(bays < (float)INT_MAX))   return cres_error(int, CERR_TOO_LARGE, .fmt = "skylight with too many bays");

    return cres_val(int, (int)bays);
}

/*
 * struct skylight_grid - how the beams divide an aperture
 * @lo:     the aperture's bounding box in the frame's (a, c), low corner
 * @hi:     its high corner
 * @na:     bays along +a
 * @nc:     bays along +c
 */
typedef struct skylight_grid {
    vec2    lo;
    vec2    hi;
    int     na;
    int     nc;
} skylight_grid;

/* the grid over @aperture, a regular polygon of @n points */
static cerr skylight_grid_get(const pcg_skylight_params *p, const vec2 *aperture, int n, skylight_grid *g)
{
    poly2d_bbox(aperture, n, g->lo, g->hi);

    float ext_a = g->hi[0] - g->lo[0], ext_c = g->hi[1] - g->lo[1];
    g->nc = CRES_RET_CERR(skylight_bays(ext_c, p->pitch));
    g->na = CRES_RET_CERR(skylight_bays(ext_a, p->pitch));
    if ((g->nc > 1 && !(ext_c / g->nc > p->beam_w)) || (g->na > 1 && !(ext_a / g->na > p->beam_w)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "skylight bays no wider than the beams between them");

    return CERR_OK;
}

cerr pcg_skylight_check(const pcg_skylight_params *p, const pcg_skylight_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(skylight_site_check(p, site));

    int n = site->sides;
    LOCAL_SET(void, buf) = mem_alloc(sizeof(vec2), .nr = (size_t)n);
    if (!buf)   return CERR_NOMEM;

    vec2 *aperture = buf;
    poly2d_regular(aperture, n, site->r, poly2d_phase_edge0_neg_z(n));

    skylight_grid g;
    return skylight_grid_get(p, aperture, n, &g);
}

cerr _pcg_skylight_emit(const pcg_ctx *ctx, const pcg_skylight_params *p, const pcg_skylight_site *site)
{
    if (!ctx || !p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(skylight_site_check(p, site));

    int n = site->sides;
    float r = site->r, phase = poly2d_phase_edge0_neg_z(n);
    /* the curb's outline: the aperture's polygon, its apothem curb_w longer */
    float apothem = r * cosf((float)M_PI / n);
    float r_out = r * (apothem + p->curb_w) / apothem;

    /* the curb's outline and its hole, wound against it, then the aperture itself */
    LOCAL_SET(void, buf) = mem_alloc(sizeof(vec2), .nr = 3 * (size_t)n);
    if (!buf)   return CERR_NOMEM;

    vec2 *pts = buf, *aperture = &pts[2 * n];
    for (int j = 0; j < n; j++) {
        poly2d_regular_vertex(pts[j], j, n, r_out, phase);
        poly2d_regular_vertex(pts[n + j], -j, n, r, phase);
        poly2d_regular_vertex(aperture[j], j, n, r, phase);
    }

    skylight_grid g;
    CERR_RET_CERR(skylight_grid_get(p, aperture, n, &g));

    const float *lo = g.lo, *hi = g.hi;
    int na = g.na, nc = g.nc;

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    const pcg_surface skip = { .skip = true };

    if (!p->frame.skip) {
        const poly_ring rings[2] = { { 0, (size_t)n }, { (size_t)n, (size_t)n } };
        const vec2 org = { 0.0f, 0.0f }, along_a = { 1.0f, 0.0f }, along_c = { 0.0f, 1.0f };
        float hw = p->beam_w * 0.5f;

        CERR_RET_CERR(pcg_plate(&at, .pts = pts, .nr_pts = 2 * (size_t)n, .rings = rings, .nr_rings = 2,
                                .y0 = 0.0f, .y1 = p->curb_h, .top = p->frame, .bottom = p->frame,
                                .edge = p->frame));

        /* beams along +a, clipped to the aperture's chord, bearing into the curb */
        for (int i = 1; i < nc; i++) {
            float c = lo[1] + i * (hi[1] - lo[1]) / nc;
            float a0, a1;

            if (poly2d_chord(aperture, n, org, along_a, c, &a0, &a1) < p->beam_w)   continue;
            CERR_RET_CERR(pcg_box(&at, .lo = { a0 - p->embed, p->curb_h - p->beam_h, c - hw },
                                  .hi = { a1 + p->embed, p->curb_h, c + hw }, .face = PCG_BOX_ALL(&p->frame)));
        }

        /* beams along +c, a little lower to dodge coplanar tops */
        for (int i = 1; i < na; i++) {
            float a = lo[0] + i * (hi[0] - lo[0]) / na;
            float c0, c1;

            /* +c's left is -a, so the line through a is -a off it */
            if (poly2d_chord(aperture, n, org, along_c, -a, &c0, &c1) < p->beam_w)  continue;
            CERR_RET_CERR(pcg_box(&at, .lo = { a - hw, p->curb_h - p->beam_h, c0 - p->embed },
                                  .hi = { a + hw, p->curb_h - p->cross_drop, c1 + p->embed },
                                  .face = PCG_BOX_ALL(&p->frame)));
        }
    }

    /* the grid's bays, a pitch of texture each way */
    const pcg_chart grid = {
        .org    = { lo[0], lo[1] },
        .scale  = { p->pitch * max(na, 1) / (hi[0] - lo[0]), p->pitch * max(nc, 1) / (hi[1] - lo[1]) },
    };
    float y = p->curb_h + p->lift;

    return pcg_plate(&at, .pts = pts, .nr_pts = (size_t)n, .y0 = y, .y1 = y, .chart = &grid, .fan = true,
                     .top = skip, .bottom = p->diffuser, .edge = skip);
}
