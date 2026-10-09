// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "pcg/internal.h"
#include "pcg/shape.h"
#include "pcg/wall.h"

void pcg_wall_params_init(pcg_wall_params *p)
{
    const pcg_surface cladding = { .role = PCG_ROLE_CLADDING, .collide = PCG_COLLIDE_SOLID };

    *p = (pcg_wall_params){
        .t      = 0.09f,
        .face_l = cladding,
        .face_r = cladding,
        .reveal = cladding,
    };
}

/* one piece of the wall over [@a0, @a1] x [@y0, @y1] */
static cerr wall_box(const pcg_ctx *ctx, const pcg_wall_params *p, float a0, float a1, float y0, float y1)
{
    /* faces in the order +a, -a, top, bottom, +c, -c */
    return pcg_box(ctx, .lo = { a0, y0, 0.0f }, .hi = { a1, y1, p->t },
                   .face = { &p->reveal, &p->reveal, &p->reveal, &p->reveal, &p->face_l, &p->face_r });
}

cerr pcg_wall_check(const pcg_wall_params *p, const pcg_wall_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    if (!pcg_positive_finite(p->t)) return CERR_INVALID_ARGUMENTS_REASON(.fmt = "wall t isn't positive and finite");
    if (!pcg_positive(site->len) || !pcg_positive(site->h) || !isfinite(site->len) || !isfinite(site->h) ||
        !isfinite(site->a0))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "wall length, height or start not finite, or no length or height");
    CERR_RET_CERR(pcg_openings_check(site->openings, site->nr_openings, site->len));

    for (size_t i = 0; i < site->nr_openings; i++)
        if (!isfinite(site->openings[i].h))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "wall opening %zu has a head that isn't finite",
                .arg0   = (void *)(uintptr_t)i
            );

    return CERR_OK;
}

cerr _pcg_wall_emit(const pcg_ctx *ctx, const pcg_wall_params *p, const pcg_wall_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_wall_check(p, site));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    float a0 = site->a0, h = site->h, run = 0.0f;

    /* the full-height pieces between the openings, then the headers over them */
    for (size_t i = 0; i <= site->nr_openings; i++) {
        pcg_span s = i < site->nr_openings ? pcg_opening_span(&site->openings[i]) :
                                             (pcg_span){ .lo = site->len };

        if (s.lo > run)     CERR_RET_CERR(wall_box(&at, p, a0 + run, a0 + s.lo, 0.0f, h));
        run = s.hi;
    }

    for (size_t i = 0; i < site->nr_openings; i++) {
        const pcg_opening *o = &site->openings[i];
        pcg_span s = pcg_opening_span(o);

        if (o->h > 0.0f && o->h < h)    CERR_RET_CERR(wall_box(&at, p, a0 + s.lo, a0 + s.hi, o->h, h));
    }

    return CERR_OK;
}
