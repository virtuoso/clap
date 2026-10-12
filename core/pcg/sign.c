// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "pcg/internal.h"
#include "pcg/sign.h"

void pcg_sign_params_init(pcg_sign_params *p)
{
    *p = (pcg_sign_params){
        .h              = 0.32f,
        .center_y       = 1.5f,
        .standoff       = 0.02f,
        .glass_standoff = 0.05f,
        .door_offset    = 0.30f,
        .plate          = { .role = PCG_ROLE_SIGN, .collide = PCG_COLLIDE_NONE },
    };
}

/* where the sign's center falls along the run */
static float sign_at(const pcg_sign_params *p, const pcg_sign_site *site)
{
    /* beside the doorway rather than over it */
    return site->door ? site->door->at + site->door->w * 0.5f + p->door_offset : site->len * 0.5f;
}

cerr pcg_sign_check(const pcg_sign_params *p, const pcg_sign_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));

    const float dims[] = { p->h, p->center_y, p->standoff, p->glass_standoff, p->door_offset, site->a0, site->len };
    if (!pcg_floats_finite(dims, array_size(dims)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "sign dimension isn't finite");

    if (!pcg_positive(p->h))    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "sign h isn't positive");
    if (!pcg_ordered(p->h * 0.5f, p->center_y))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "sign center_y below half its height");
    if (!pcg_nonnegative(p->standoff) || !pcg_nonnegative(p->glass_standoff) || !pcg_nonnegative(p->door_offset))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "sign standoff or door_offset is negative");

    if (!pcg_positive(site->len))       return CERR_INVALID_ARGUMENTS_REASON(.fmt = "sign on a run of no length");
    if (!site->text || !*site->text)    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "sign without text");
    if (site->door)                     CERR_RET_CERR(pcg_openings_check(site->door, 1, site->len));
    if (!pcg_ordered(sign_at(p, site), site->len))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "sign past the end of its run");

    return CERR_OK;
}

cerr _pcg_sign_emit(const pcg_ctx *ctx, const pcg_sign_params *p, const pcg_sign_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_sign_check(p, site));

    float at = sign_at(p, site);
    if (p->plate.skip)  return CERR_OK;

    /* the label in the site frame's parent, which is @ctx's */
    const pcg_frame local = {
        .org = { site->a0 + at, p->center_y, site->glass ? p->glass_standoff : p->standoff },
        .dir = { 1.0f, 0.0f },
    };
    pcg_frame label;
    pcg_frame_compose(&site->frame, &local, &label);

    return pcg_sink_label(ctx, &p->plate, &label, p->h, site->text);
}
