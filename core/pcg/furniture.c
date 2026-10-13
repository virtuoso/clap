// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "pcg/furniture.h"
#include "pcg/internal.h"
#include "pcg/shape.h"

/* every one of @nr dimensions is positive */
static bool furniture_positive(const float *dims, size_t nr)
{
    for (size_t i = 0; i < nr; i++)
        if (!pcg_positive(dims[i]))     return false;

    return true;
}

/****************************************************************************
 * Desk
 ****************************************************************************/

void pcg_desk_params_init(pcg_desk_params *p)
{
    *p = (pcg_desk_params){
        .w          = 1.5f,
        .d          = 0.7f,
        .h          = 0.72f,
        .top_t      = 0.06f,
        .side_w     = 0.12f,
        .side_inset = 0.02f,
        .side_d     = 0.62f,
        .shell      = { .role = PCG_ROLE_FURNITURE, .collide = PCG_COLLIDE_NONE },
    };
}

cerr pcg_desk_check(const pcg_desk_params *p, const pcg_desk_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));

    const float dims[] = { p->w, p->d, p->h, p->top_t, p->side_w, p->side_d };
    if (!pcg_floats_finite(dims, array_size(dims)) || !isfinite(p->side_inset))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "desk dimension isn't finite");
    if (!furniture_positive(dims, array_size(dims)) || !pcg_nonnegative(p->side_inset))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "desk dimension isn't positive, or its side inset is negative");
    if (!(p->top_t < p->h))     return CERR_INVALID_ARGUMENTS_REASON(.fmt = "desk top as thick as the desk is high");
    if (!pcg_ordered(p->side_d, p->d))  return CERR_INVALID_ARGUMENTS_REASON(.fmt = "desk sides deeper than its top");
    if (!pcg_ordered(p->side_inset + p->side_w, p->w * 0.5f))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "desk sides don't fit side by side under its top");

    return CERR_OK;
}

cerr _pcg_desk_emit(const pcg_ctx *ctx, const pcg_desk_params *p, const pcg_desk_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_desk_check(p, site));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    float a1 = p->w * 0.5f, c1 = p->d * 0.5f, y0 = p->h - p->top_t;
    /* each side runs from its outer face, @side_inset in from the top's end, @side_w inward */
    float so = a1 - p->side_inset, si = so - p->side_w, sc = p->side_d * 0.5f;

    CERR_RET_CERR(pcg_box(&at, .lo = { -a1, y0, -c1 }, .hi = { a1, p->h, c1 }, .face = PCG_BOX_ALL(&p->shell)));
    CERR_RET_CERR(pcg_box(&at, .lo = { -so, 0.0f, -sc }, .hi = { -si, y0, sc }, .face = PCG_BOX_ALL(&p->shell)));

    return pcg_box(&at, .lo = { si, 0.0f, -sc }, .hi = { so, y0, sc }, .face = PCG_BOX_ALL(&p->shell));
}

/****************************************************************************
 * Chair
 ****************************************************************************/

void pcg_chair_params_init(pcg_chair_params *p)
{
    *p = (pcg_chair_params){
        .seat_w     = 0.44f,
        .seat_d     = 0.44f,
        .seat_h     = 0.48f,
        .seat_t     = 0.06f,
        .seat_c     = 0.02f,
        .back_w     = 0.42f,
        .back_h     = 0.40f,
        .back_t     = 0.06f,
        .stem_r     = 0.035f,
        .base_r     = 0.24f,
        .base_h     = 0.05f,
        .segments   = 10,
        .shell      = { .role = PCG_ROLE_FURNITURE, .collide = PCG_COLLIDE_NONE },
        .base       = { .role = PCG_ROLE_FURNITURE, .collide = PCG_COLLIDE_NONE },
    };
}

cerr pcg_chair_check(const pcg_chair_params *p, const pcg_chair_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));

    const float dims[] = {
        p->seat_w, p->seat_d, p->seat_h, p->seat_t, p->back_w, p->back_h, p->back_t, p->stem_r, p->base_r,
        p->base_h,
    };
    if (!pcg_floats_finite(dims, array_size(dims)) || !isfinite(p->seat_c))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "chair dimension isn't finite");
    if (!furniture_positive(dims, array_size(dims)) || p->segments < 3)
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "chair dimension isn't positive, or fewer than 3 segments");
    if (p->segments > PCG_CYLINDER_SIDES_MAX)
        return CERR_TOO_LARGE_REASON(.fmt = "chair of more segments than a mesh holds");
    if (!pcg_positive(p->seat_h - p->seat_t - p->base_h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "chair seat too low for its disc and its thickness");

    return CERR_OK;
}

cerr _pcg_chair_emit(const pcg_ctx *ctx, const pcg_chair_params *p, const pcg_chair_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_chair_check(p, site));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    float sa = p->seat_w * 0.5f, sy = p->seat_h - p->seat_t;
    float s0 = p->seat_c - p->seat_d * 0.5f, s1 = p->seat_c + p->seat_d * 0.5f;
    float ba = p->back_w * 0.5f;

    CERR_RET_CERR(pcg_box(&at, .lo = { -sa, sy, s0 }, .hi = { sa, p->seat_h, s1 }, .face = PCG_BOX_ALL(&p->shell)));
    /* the back stands behind the seat's rear edge, from the seat's top up */
    CERR_RET_CERR(pcg_box(&at, .lo = { -ba, p->seat_h, s0 - p->back_t }, .hi = { ba, p->seat_h + p->back_h, s0 },
                          .face = PCG_BOX_ALL(&p->shell)));
    CERR_RET_CERR(pcg_cylinder(&at, .org = { 0.0f, p->base_h, 0.0f }, .h = sy - p->base_h, .r = p->stem_r,
                               .sides = p->segments, .uv = PCG_UV_METERS, .surf = p->base));

    return pcg_cylinder(&at, .org = { 0.0f, 0.0f, 0.0f }, .h = p->base_h, .r = p->base_r, .sides = p->segments,
                        .uv = PCG_UV_METERS, .surf = p->base);
}

/****************************************************************************
 * Monitor
 ****************************************************************************/

void pcg_monitor_params_init(pcg_monitor_params *p)
{
    *p = (pcg_monitor_params){
        .w          = 0.54f,
        .h          = 0.36f,
        .t          = 0.05f,
        .lift       = 0.10f,
        .front      = 0.005f,
        .bezel      = 0.025f,
        .screen_off = 0.003f,
        .foot_w     = 0.22f,
        .foot_d     = 0.16f,
        .foot_t     = 0.02f,
        .foot_c     = -0.02f,
        .neck_w     = 0.05f,
        .neck_d     = 0.04f,
        .neck_h     = 0.14f,
        .neck_c     = -0.04f,
        .shell_uv   = { 0.05f, 0.05f, 0.40f, 0.40f },
        .screen_uv  = { 0.98f, 0.95f, 0.52f, 0.05f },
        .shell      = { .role = PCG_ROLE_SCREEN, .collide = PCG_COLLIDE_NONE },
        .screen     = { .role = PCG_ROLE_SCREEN, .collide = PCG_COLLIDE_NONE },
    };
}

cerr pcg_monitor_check(const pcg_monitor_params *p, const pcg_monitor_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));

    const float sizes[] = {
        p->w, p->h, p->t, p->screen_off, p->foot_w, p->foot_d, p->foot_t, p->neck_w, p->neck_d, p->neck_h,
    };
    const float others[] = { p->lift, p->front, p->bezel, p->foot_c, p->neck_c };
    if (!pcg_floats_finite(sizes, array_size(sizes)) || !pcg_floats_finite(others, array_size(others)) ||
        !pcg_floats_finite(p->shell_uv, 4) || !pcg_floats_finite(p->screen_uv, 4))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "monitor dimension or texture coordinate isn't finite");
    if (!furniture_positive(sizes, array_size(sizes)) || !pcg_nonnegative(p->lift) || !pcg_nonnegative(p->bezel))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "monitor size isn't positive, or its lift or bezel is negative");
    if (!(p->bezel * 2.0f < p->w) || !(p->bezel * 2.0f < p->h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "monitor bezel leaves no screen");
    if (!(p->foot_t < p->neck_h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "monitor neck doesn't rise above its foot");

    return CERR_OK;
}

cerr _pcg_monitor_emit(const pcg_ctx *ctx, const pcg_monitor_params *p, const pcg_monitor_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_monitor_check(p, site));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    float fa = p->foot_w * 0.5f, fc = p->foot_d * 0.5f;
    float na = p->neck_w * 0.5f, nc = p->neck_d * 0.5f;
    float pa = p->w * 0.5f, py = p->lift + p->h;

    CERR_RET_CERR(pcg_box(&at, .lo = { -fa, 0.0f, p->foot_c - fc }, .hi = { fa, p->foot_t, p->foot_c + fc },
                          .face = PCG_BOX_ALL(&p->shell), .uv_rect = p->shell_uv));
    CERR_RET_CERR(pcg_box(&at, .lo = { -na, p->foot_t, p->neck_c - nc }, .hi = { na, p->neck_h, p->neck_c + nc },
                          .face = PCG_BOX_ALL(&p->shell), .uv_rect = p->shell_uv));
    CERR_RET_CERR(pcg_box(&at, .lo = { -pa, p->lift, p->front - p->t }, .hi = { pa, py, p->front },
                          .face = PCG_BOX_ALL(&p->shell), .uv_rect = p->shell_uv));

    /* the screen, inside the bezel, just proud of the panel's front */
    float sa = pa - p->bezel, s0 = p->lift + p->bezel, s1 = py - p->bezel, sc = p->front + p->screen_off;
    const float *r = p->screen_uv;

    return pcg_quad(&at, .c00 = { -sa, s0, sc }, .c10 = { sa, s0, sc }, .c11 = { sa, s1, sc }, .c01 = { -sa, s1, sc },
                    .uv_rect = { r[0], r[1], r[2], r[3] }, .uv = PCG_UV_UNIT, .surf = p->screen);
}

/****************************************************************************
 * Corkboard
 ****************************************************************************/

void pcg_corkboard_params_init(pcg_corkboard_params *p)
{
    *p = (pcg_corkboard_params){
        .w          = 0.96f,
        .h          = 0.68f,
        .t          = 0.035f,
        .center_y   = 1.55f,
        .standoff   = 0.005f,
        .face_uv    = { 0.0f, 1.0f, 1.0f, 0.0f },
        .rim_uv     = { 0.005f, 0.4f, 0.01f, 0.6f },
        .board      = { .role = PCG_ROLE_BOARD, .collide = PCG_COLLIDE_NONE },
    };
}

cerr pcg_corkboard_check(const pcg_corkboard_params *p, const pcg_corkboard_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_frame_check(&site->frame));

    const float dims[] = { p->w, p->h, p->t, p->center_y, p->standoff };
    if (!pcg_floats_finite(dims, array_size(dims)) || !pcg_floats_finite(p->face_uv, 4) ||
        !pcg_floats_finite(p->rim_uv, 4))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "corkboard dimension or texture coordinate isn't finite");
    if (!furniture_positive(dims, 3) || !pcg_nonnegative(p->standoff))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "corkboard size isn't positive, or its standoff is negative");
    if (!pcg_ordered(p->h * 0.5f, p->center_y))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "corkboard hanging into the floor");

    return CERR_OK;
}

/* one face of a corkboard, corners counterclockwise from outside */
static cerr corkboard_face(const pcg_ctx *at, const pcg_surface *surf, const vec4 rect,
                           const vec3 c00, const vec3 c10, const vec3 c11, const vec3 c01)
{
    return pcg_quad(at, .c00 = { c00[0], c00[1], c00[2] }, .c10 = { c10[0], c10[1], c10[2] },
                    .c11 = { c11[0], c11[1], c11[2] }, .c01 = { c01[0], c01[1], c01[2] },
                    .uv_rect = { rect[0], rect[1], rect[2], rect[3] }, .uv = PCG_UV_UNIT, .surf = *surf);
}

cerr _pcg_corkboard_emit(const pcg_ctx *ctx, const pcg_corkboard_params *p, const pcg_corkboard_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_corkboard_check(p, site));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    const pcg_surface *s = &p->board;
    float x0 = -p->w * 0.5f, x1 = p->w * 0.5f;
    float y0 = p->center_y - p->h * 0.5f, y1 = p->center_y + p->h * 0.5f;
    float z0 = p->standoff, z1 = p->standoff + p->t;

    /* the front maps the face rectangle, the back and the edges stay on the rim */
    CERR_RET_CERR(corkboard_face(&at, s, p->face_uv, (vec3){ x0, y0, z1 }, (vec3){ x1, y0, z1 },
                                 (vec3){ x1, y1, z1 }, (vec3){ x0, y1, z1 }));
    CERR_RET_CERR(corkboard_face(&at, s, p->rim_uv, (vec3){ x1, y0, z0 }, (vec3){ x0, y0, z0 },
                                 (vec3){ x0, y1, z0 }, (vec3){ x1, y1, z0 }));
    CERR_RET_CERR(corkboard_face(&at, s, p->rim_uv, (vec3){ x1, y0, z1 }, (vec3){ x1, y0, z0 },
                                 (vec3){ x1, y1, z0 }, (vec3){ x1, y1, z1 }));
    CERR_RET_CERR(corkboard_face(&at, s, p->rim_uv, (vec3){ x0, y0, z0 }, (vec3){ x0, y0, z1 },
                                 (vec3){ x0, y1, z1 }, (vec3){ x0, y1, z0 }));
    CERR_RET_CERR(corkboard_face(&at, s, p->rim_uv, (vec3){ x0, y1, z1 }, (vec3){ x1, y1, z1 },
                                 (vec3){ x1, y1, z0 }, (vec3){ x0, y1, z0 }));

    return corkboard_face(&at, s, p->rim_uv, (vec3){ x0, y0, z0 }, (vec3){ x1, y0, z0 },
                          (vec3){ x1, y0, z1 }, (vec3){ x0, y0, z1 });
}
