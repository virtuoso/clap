// SPDX-License-Identifier: Apache-2.0
#include <float.h>
#include <math.h>
#include "pcg/internal.h"
#include "pcg/shape.h"
#include "pcg/stair.h"

void pcg_stair_params_init(pcg_stair_params *p)
{
    const pcg_surface structure = { .role = PCG_ROLE_STRUCTURE, .collide = PCG_COLLIDE_SOLID };

    *p = (pcg_stair_params){
        .width      = 1.2f,
        .riser_max  = 0.17f,
        .going      = 0.28f,
        .waist      = 0.15f,
        .landing_t  = 0.2f,
        .tread      = structure,
        .riser      = structure,
        .side       = structure,
        .soffit     = structure,
        .landing    = structure,
    };
}

cres(int) pcg_stair_steps(const pcg_stair_params *p, float rise)
{
    if (!p)     return cres_error(int, CERR_INVALID_ARGUMENTS);
    if (!pcg_positive_finite(rise) || !pcg_positive_finite(p->riser_max))
        return cres_error(int, CERR_INVALID_ARGUMENTS, .fmt = "stair rise or riser_max isn't positive and finite");

    float q = rise / p->riser_max;
    if (!(q < ldexpf(1.0f, FLT_MANT_DIG)))
        return cres_error(int, CERR_TOO_LARGE, .fmt = "stair of more risers than a float counts");

    /* the candidate is one off at most; the definition settles it */
    int n = (int)ceilf(q);
    if (n > 1 && rise / (n - 1) <= p->riser_max)    n--;
    else if (!(rise / n <= p->riser_max))           n++;

    return cres_val(int, n);
}

cres(float) pcg_stair_run(const pcg_stair_params *p, float rise)
{
    int steps = CRES_RET_SCALAR(pcg_stair_steps(p, rise), float);

    return cres_val(float, steps * p->going);
}

cerr pcg_stair_params_check(const pcg_stair_params *p)
{
    if (!p)     return CERR_INVALID_ARGUMENTS;
    if (!pcg_positive(p->width) || !pcg_positive(p->going) || !pcg_positive(p->waist) ||
        !pcg_positive(p->landing_t) || !isfinite(p->width) || !isfinite(p->going) || !isfinite(p->waist) ||
        !isfinite(p->landing_t))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "stair width, going, waist or landing_t isn't positive and finite");
    if (!pcg_positive_finite(p->riser_max))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "stair riser_max isn't positive and finite");

    return CERR_OK;
}

cerr pcg_stair_check(const pcg_stair_params *p, const pcg_stair_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_stair_params_check(p));
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    if (!pcg_nonnegative(site->land_lo) || !pcg_nonnegative(site->land_hi) || !isfinite(site->land_lo) ||
        !isfinite(site->land_hi))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "stair landing is negative or isn't finite");
    CRES_RET_CERR(pcg_stair_steps(p, site->rise));

    return CERR_OK;
}

cerr _pcg_stair_emit(const pcg_ctx *ctx, const pcg_stair_params *p, const pcg_stair_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_stair_check(p, site));

    int steps = CRES_RET_CERR(pcg_stair_steps(p, site->rise));
    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    float rs = site->rise / steps, hw = p->width * 0.5f, lt = p->landing_t;

    if (site->land_lo > 0.0f)
        CERR_RET_CERR(pcg_box(&at, .lo = { -site->land_lo, -lt, -hw }, .hi = { 0.0f, 0.0f, hw },
                              .face = PCG_BOX_ALL(&p->landing)));
    if (site->land_hi > 0.0f) {
        float top = steps * p->going;

        CERR_RET_CERR(pcg_box(&at, .lo = { top, site->rise - lt, -hw }, .hi = { top + site->land_hi, site->rise, hw },
                              .face = PCG_BOX_ALL(&p->landing)));
    }

    /*
     * Faces in the order +a, -a, top, bottom, +c, -c. A step's +a end is
     * the soffit's riser under the next tread; the rest of it lies face to
     * face with the next step's riser end
     */
    for (int k = 0; k < steps; k++)
        CERR_RET_CERR(pcg_box(&at, .lo = { k * p->going, k * rs - p->waist, -hw },
                              .hi = { (k + 1) * p->going, (k + 1) * rs, hw },
                              .face = { &p->soffit, &p->riser, &p->tread, &p->soffit, &p->side, &p->side }));

    return CERR_OK;
}
