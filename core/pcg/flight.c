// SPDX-License-Identifier: Apache-2.0
#include <float.h>
#include <math.h>
#include "memory.h"
#include "poly2d.h"
#include "pcg/flight.h"
#include "pcg/internal.h"
#include "pcg/shape.h"

/** FLIGHT_APRON_CLIPS - half-planes an apron is clipped by: its two along ends and the far edge */
#define FLIGHT_APRON_CLIPS  3

void pcg_flight_params_init(pcg_flight_params *p)
{
    *p = (pcg_flight_params){
        .depart         = { .role = PCG_ROLE_FLOOR, .collide = PCG_COLLIDE_SOLID },
        .arrive         = { .role = PCG_ROLE_STRUCTURE, .collide = PCG_COLLIDE_SOLID },
        .gap            = 0.03f,
        .rail_inset     = 0.03f,
        .rim_clear      = 0.06f,
        .min_landing    = 0.6f,
        .min_apron      = 0.05f,
        .air            = 0.1f,
        .rail_near      = true,
        .rail_far       = true,
    };
    pcg_stair_params_init(&p->stair);
    pcg_balustrade_params_init(&p->rail);
}

static cerr flight_params_check(const pcg_flight_params *p)
{
    CERR_RET_CERR(pcg_stair_params_check(&p->stair));
    CERR_RET_CERR(pcg_balustrade_params_check(&p->rail));

    const float dims[] = { p->gap, p->rail_inset, p->rim_clear, p->min_landing, p->min_apron, p->air };
    if (!pcg_floats_finite(dims, array_size(dims)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "flight dimension isn't finite");

    if (!pcg_nonnegative(p->gap) || !pcg_nonnegative(p->rim_clear) || !pcg_nonnegative(p->min_landing) ||
        !pcg_nonnegative(p->min_apron) || !pcg_nonnegative(p->air))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "flight gap, clearance, minimum or air is negative");

    return CERR_OK;
}

/*
 * Every point of @v lies on the same side of every edge's line, or on it,
 * and not all of them on it: a convex polygon with an area, wound either
 * way, that winds around once. A point closer to the line than a float
 * rounds its coordinates counts as on it, the edge's own ends included.
 */
static bool flight_void_convex(const vec2 *v, size_t nr)
{
    bool left = false, right = false;

    for (size_t i = 0; i < nr; i++) {
        const float *a = v[i], *b = v[(i + 1) % nr];
        double ex = (double)b[0] - a[0], ez = (double)b[1] - a[1];

        for (size_t k = 0; k < nr; k++) {
            double dx = (double)v[k][0] - a[0], dz = (double)v[k][1] - a[1];
            double side = ex * dz - ez * dx, tol = FLT_EPSILON * (fabs(ex) + fabs(ez)) * (fabs(dx) + fabs(dz));

            left |= side > tol;
            right |= side < -tol;
        }
    }

    return left != right;
}

static cerr flight_void_check(const vec2 *v, size_t nr, const char *which)
{
    /* a floor without a void leaves it out */
    if (!v && !nr)  return CERR_OK;
    CERR_RET_CERR(pcg_path_check(v, nr, true, which));

    /* the aprons clip it and fan it out from its first point */
    if (!flight_void_convex(v, nr)) return CERR_INVALID_ARGUMENTS_REASON(.fmt = "%s isn't convex", .arg0 = which);

    return CERR_OK;
}

/*
 * struct flight_fit - how a flight sits in its chord
 * @steps:  riser count
 * @run:    run length along the chord
 * @land:   landing length at either end
 */
typedef struct flight_fit {
    int     steps;
    float   run;
    float   land;
} flight_fit;

/* check @p and @site and fit the run into the chord */
static cerr flight_fit_get(const pcg_flight_params *p, const pcg_flight_site *site, flight_fit *fit)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(flight_params_check(p));
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    if (!pcg_positive_finite(site->len))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "flight chord isn't positive and finite");
    CERR_RET_CERR(flight_void_check(site->depart_void, site->nr_depart_void, "flight departure void"));
    CERR_RET_CERR(flight_void_check(site->arrive_void, site->nr_arrive_void, "flight arrival void"));

    fit->steps = CRES_RET_CERR(pcg_stair_steps(&p->stair, site->rise));
    fit->run = CRES_RET_CERR(pcg_stair_run(&p->stair, site->rise));
    fit->land = (site->len - fit->run) * 0.5f;
    if (!(fit->land >= p->min_landing))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "flight run leaves landings under min_landing in its chord");

    return CERR_OK;
}

cerr pcg_flight_check(const pcg_flight_params *p, const pcg_flight_site *site)
{
    flight_fit fit;

    return flight_fit_get(p, site, &fit);
}

/* a boarding window of the flight at @site: @along on the chord, out to the far edge across it */
static void flight_window(const pcg_flight_params *p, const pcg_flight_site *site, const pcg_range *along,
                          pcg_region *out)
{
    *out = (pcg_region){
        .frame  = site->frame,
        .along  = *along,
        .across = { .hi = p->gap + p->stair.width, .open_lo = true },
    };
}

cerr pcg_flight_depart_cut(const pcg_flight_params *p, const pcg_flight_site *site, float stand_h,
                           pcg_region *out)
{
    if (!out)   return CERR_INVALID_ARGUMENTS;

    flight_fit fit;
    CERR_RET_CERR(flight_fit_get(p, site, &fit));
    if (!pcg_nonnegative_finite(stand_h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "flight stand_h is negative or isn't finite");

    float H = site->rise;
    pcg_range along = {
        .hi         = fit.land + fit.run * (stand_h + p->stair.waist + H / fit.steps + p->air) / H,
        .open_lo    = true,
    };

    flight_window(p, site, &along, out);

    return CERR_OK;
}

cerr pcg_flight_arrive_cut(const pcg_flight_params *p, const pcg_flight_site *site, float headroom,
                           float hang, pcg_region *out)
{
    if (!out)   return CERR_INVALID_ARGUMENTS;

    flight_fit fit;
    CERR_RET_CERR(flight_fit_get(p, site, &fit));
    if (!pcg_nonnegative_finite(headroom) || !pcg_nonnegative_finite(hang))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "flight headroom or hang is negative or isn't finite");

    float slope = site->rise / fit.steps / p->stair.going;
    pcg_range along = {
        .lo         = site->len - fit.land - (headroom + hang + p->air) / slope,
        .open_hi    = true,
    };

    flight_window(p, site, &along, out);

    return CERR_OK;
}

/*
 * struct flight_apron - an apron's outline
 * @pts:    the outline in the site frame's parent's (x, z)
 * @nr:     number of points, 0 for no apron
 */
typedef struct flight_apron {
    vec2    *pts;
    size_t  nr;
} flight_apron;

/*
 * The part of the convex void @v inside @along by (-inf, @across] of the
 * site's frame, one Sutherland-Hodgman step per bounded end, in storage
 * allocated into @buf: nothing if it reaches less than the style's
 * min_apron along or across, which leaves out the clips without an area
 */
static cerr flight_apron_clip(const pcg_flight_params *p, const pcg_flight_site *site, const vec2 *v, size_t nr,
                              const pcg_range *along, float across, void **buf, flight_apron *out)
{
    *out = (flight_apron){};
    if (!v)     return CERR_OK;

    /* a convex polygon gains a point per clip at most */
    size_t cap = nr + FLIGHT_APRON_CLIPS;
    *buf = mem_alloc(sizeof(vec2), .nr = 2 * cap);
    if (!*buf)  return CERR_NOMEM;

    const pcg_frame *f = &site->frame;
    vec2 in;
    pcg_frame_across(f, in);

    float dA = f->org[0] * f->dir[0] + f->org[2] * f->dir[1];
    float aA = f->org[0] * in[0] + f->org[2] * in[1];
    poly2d_halfplane hp[FLIGHT_APRON_CLIPS];
    size_t nr_hp = 0;

    if (!along->open_lo)    hp[nr_hp++] = (poly2d_halfplane){ .n = { -f->dir[0], -f->dir[1] }, .d = -(dA + along->lo) };
    if (!along->open_hi)    hp[nr_hp++] = (poly2d_halfplane){ .n = { f->dir[0], f->dir[1] }, .d = dA + along->hi };
    hp[nr_hp++] = (poly2d_halfplane){ .n = { in[0], in[1] }, .d = aA + across };

    vec2 *bufs[2] = { *buf, (vec2 *)*buf + cap }, *pts = nullptr;
    const vec2 *src = v;
    size_t np = nr;

    for (size_t k = 0; k < nr_hp; k++) {
        pts = bufs[k & 1];
        np = CRES_RET_CERR(poly2d_clip(src, np, &hp[k], pts, cap));
        src = pts;
    }
    if (np < 3)     return CERR_OK;

    float mn = 0.0f, mx = 0.0f, amn = 0.0f, amx = 0.0f;
    for (size_t j = 0; j < np; j++) {
        float ac = pts[j][0] * in[0] + pts[j][1] * in[1] - aA;
        float al = pts[j][0] * f->dir[0] + pts[j][1] * f->dir[1] - dA;

        if (!j) {
            mn = mx = ac;
            amn = amx = al;
            continue;
        }
        mn = fminf(mn, ac);
        mx = fmaxf(mx, ac);
        amn = fminf(amn, al);
        amx = fmaxf(amx, al);
    }
    if (mx - mn < p->min_apron || amx - amn < p->min_apron)     return CERR_OK;

    *out = (flight_apron){ .pts = pts, .nr = np };

    return CERR_OK;
}

/* a level landing rail on the far rail line, from the rim to the run */
typedef struct flight_landing {
    float   a;
    float   len;
} flight_landing;

/* where the far rail line crosses @v, the void; false if it doesn't */
static bool flight_rail_chord(const pcg_frame *f, const vec2 *v, size_t nr, float c, float *lo, float *hi)
{
    if (!v)     return false;

    /* poly2d_chord() measures across along the left normal, which a mirrored frame's +c is not */
    float off = f->mirror ? -c : c;

    return poly2d_chord(v, nr, (vec2){ f->org[0], f->org[2] }, f->dir, off, lo, hi) > 0.0f;
}

cerr _pcg_flight_emit(const pcg_ctx *ctx, const pcg_flight_params *p, const pcg_flight_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;

    flight_fit fit;
    CERR_RET_CERR(flight_fit_get(p, site, &fit));

    float H = site->rise, sw = p->stair.width, lt = p->stair.landing_t;
    float deep = p->gap + sw;
    float crail = deep + p->rail_inset;
    float b0, b1;

    /* everything that can still fail before anything is emitted */
    pcg_range depart_along = { .hi = fit.land, .open_lo = true };
    pcg_range arrive_along = { .lo = site->len - fit.land, .open_hi = true };
    LOCAL(void, depart_buf);
    LOCAL(void, arrive_buf);
    flight_apron depart, arrive;
    CERR_RET_CERR(flight_apron_clip(p, site, site->depart_void, site->nr_depart_void, &depart_along, deep,
                                    &depart_buf, &depart));
    CERR_RET_CERR(flight_apron_clip(p, site, site->arrive_void, site->nr_arrive_void, &arrive_along, deep,
                                    &arrive_buf, &arrive));

    flight_landing land_lo = {}, land_hi = {};
    if (flight_rail_chord(&site->frame, site->depart_void, site->nr_depart_void, crail, &b0, &b1)) {
        land_lo.a = b0 + p->rim_clear;
        land_lo.len = fit.land - (b0 + p->rim_clear);
    }
    if (flight_rail_chord(&site->frame, site->arrive_void, site->nr_arrive_void, crail, &b0, &b1)) {
        land_hi.a = site->len - fit.land;
        land_hi.len = b1 - p->rim_clear - (site->len - fit.land);
    }

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);

    CERR_RET_CERR(pcg_stair_emit(&at, &p->stair,
                                 .frame = { .org = { fit.land, 0.0f, p->gap + sw * 0.5f }, .dir = { 1.0f, 0.0f } },
                                 .rise = H));

    /* the aprons, flush with their floors, chart the parent's (x, z) */
    const pcg_frame level = { .org = { 0.0f, site->frame.org[1], 0.0f }, .dir = { 1.0f, 0.0f } };
    pcg_ctx lc = pcg_ctx_at(ctx, &level);

    if (arrive.nr)
        CERR_RET_CERR(pcg_plate(&lc, .pts = arrive.pts, .nr_pts = arrive.nr, .y0 = H - lt, .y1 = H, .fan = true,
                                .top = p->arrive, .bottom = p->arrive, .edge = p->arrive));
    if (depart.nr)
        CERR_RET_CERR(pcg_plate(&lc, .pts = depart.pts, .nr_pts = depart.nr, .y0 = -lt, .y1 = 0.0f, .fan = true,
                                .top = p->depart, .bottom = p->depart, .edge = p->depart));

    /* the sloped pair, near side then far side */
    if (p->rail_near)
        CERR_RET_CERR(pcg_balustrade_emit(&at, &p->rail,
                                          .frame = { .org = { fit.land, 0.0f, p->gap + p->rail_inset },
                                                     .dir = { 1.0f, 0.0f } },
                                          .len = fit.run, .rise = H));
    if (!p->rail_far)   return CERR_OK;

    CERR_RET_CERR(pcg_balustrade_emit(&at, &p->rail,
                                      .frame = { .org = { fit.land, 0.0f, crail }, .dir = { 1.0f, 0.0f } },
                                      .len = fit.run, .rise = H));

    /* the landings' level rails, each standing on its floor */
    if (land_lo.len > p->rail.min_run)
        CERR_RET_CERR(pcg_balustrade_emit(&at, &p->rail,
                                          .frame = { .org = { land_lo.a, 0.0f, crail }, .dir = { 1.0f, 0.0f } },
                                          .len = land_lo.len));
    if (land_hi.len > p->rail.min_run)
        CERR_RET_CERR(pcg_balustrade_emit(&at, &p->rail,
                                          .frame = { .org = { land_hi.a, 0.0f, crail }, .dir = { 1.0f, 0.0f } },
                                          .len = land_hi.len, .y = H));

    return CERR_OK;
}
