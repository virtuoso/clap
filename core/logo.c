// SPDX-License-Identifier: Apache-2.0
/*
 * The clap logo, procedurally: a horseshoe magnet stands in for the "C",
 * enveloping the rest of the lettering, with a tagline underneath. The
 * magnet is built as polygon rings and extruded via prim_emit_extrusion();
 * the lettering comes from text-mesh.
 */
#include <math.h>
#include "logo.h"
#include "memory.h"
#include "primitives.h"
#include "text-mesh.h"
#include "triangulate.h"
#include "util.h"

/* the clap logo's magnet proportions, relative to the ring radius */
#define MAGNET_BAR_RATIO    0.446f  /* bar thickness */
#define MAGNET_CUT_RATIO    0.886f  /* pole cut X position */
#define MAGNET_GAP_RATIO    0.066f  /* air gap between the body and the poles */
#define MAGNET_POLE_RATIO   0.455f  /* detached pole length */
#define MAGNET_TAPER        8.0f    /* pole convergence angle, degrees */
#define MAGNET_ROUND_RATIO  0.05f   /* pole tip outer corner rounding radius */
#define MAGNET_SEGMENTS     64      /* points per full circle */

/* lettering proportions, ditto */
#define LOGO_CAP_RATIO      0.578f  /* lettering cap height, relative to the radius */
#define LOGO_TEXT_X_RATIO   -0.071f /* lettering left edge, relative to the radius */
#define LOGO_TAG_W_RATIO    0.712f  /* tagline width, relative to the logo width */
#define LOGO_TAG_Y_RATIO    1.436f  /* tagline baseline below the center, relative to the radius */

/**
 * corner_fillet() - round a polygon corner with a circular fillet
 * @out:        output points
 * @pm:         the previous corner
 * @p:          the corner to round
 * @pn:         the next corner
 * @radius:     fillet radius, clamped to eat at most half of an edge
 * @nr_seg:     points per full circle
 *
 * Replace @p with a fillet tangent to both adjacent edges
 * (@pm -> @p -> @pn); the corner goes out as is when there's nothing to
 * round.
 * Return: the number of points emitted into @out
 */
static size_t corner_fillet(vec2 *out, const vec2 pm, const vec2 p, const vec2 pn,
                            float radius, unsigned int nr_seg)
{
    vec2 u, v;

    vec2_sub(u, p, pm);
    vec2_sub(v, pn, p);

    float ulen = vec2_len(u), vlen = vec2_len(v);
    if (radius <= 0.0f || ulen == 0.0f || vlen == 0.0f) goto sharp;

    vec2_scale(u, u, 1.0f / ulen);
    vec2_scale(v, v, 1.0f / vlen);

    float cross = u[0] * v[1] - u[1] * v[0];
    if (fabsf(cross) < 1e-6f)   goto sharp;     /* straight through or folding back */

    /* the exterior (turn) angle and the tangent point distance from the corner */
    float turn = atan2f(fabsf(cross), u[0] * v[0] + u[1] * v[1]);
    float t = radius * tanf(turn / 2.0f);

    /* don't let the fillet eat more than half of either edge */
    float t_max = fminf(ulen, vlen) / 2.0f;
    if (t > t_max) {
        t = t_max;
        radius = t / tanf(turn / 2.0f);
    }

    /* the fillet center is @radius off the tangent point, on the turn side */
    float side = cross > 0.0f ? 1.0f : -1.0f;
    vec2 t1 = { p[0] - u[0] * t, p[1] - u[1] * t };
    vec2 center = { t1[0] - u[1] * radius * side, t1[1] + u[0] * radius * side };

    float a0 = atan2f(t1[1] - center[1], t1[0] - center[0]);
    for (unsigned int i = 0; i <= nr_seg; i++) {
        float a = a0 + side * turn * i / nr_seg;

        out[i][0] = center[0] + radius * cosf(a);
        out[i][1] = center[1] + radius * sinf(a);
    }

    return nr_seg + 1;

sharp:
    out[0][0] = p[0];
    out[0][1] = p[1];

    return 1;
}

/**
 * poly_fillet() - emit a ring of corners, rounding some of them
 * @out:        output points
 * @corners:    the ring's corners
 * @radius:     per-corner rounding radius; 0 keeps a corner sharp
 * @nr:         number of corners
 * @nr_seg:     points per full circle of a fillet
 *
 * Return: the number of points emitted into @out
 */
static size_t poly_fillet(vec2 *out, const vec2 *corners, const float *radius, size_t nr,
                          unsigned int nr_seg)
{
    size_t w = 0;

    for (size_t i = 0; i < nr; i++)
        w += corner_fillet(&out[w], corners[(i + nr - 1) % nr], corners[i],
                           corners[(i + 1) % nr], radius[i], nr_seg);

    return w;
}

/**
 * magnet_radius() - ring radius of a magnet of a given width
 * @width:      total X extent, the detached poles included
 * @cut:        pole cut X position, in ring radii
 * @gap:        body-to-pole air gap, in ring radii
 * @pole_ratio: detached pole length, in ring radii
 *
 * Return: the ring radius
 */
static float magnet_radius(float width, float cut, float gap, float pole_ratio)
{
    return width / (1.0f + cut + gap + pole_ratio);
}

cerr _magnet_emit(const magnet_emit_opts *opts)
{
    if (!opts || !opts->mesh || opts->width <= 0.0f)    return CERR_INVALID_ARGUMENTS;

    float bar = opts->bar_ratio ? opts->bar_ratio : MAGNET_BAR_RATIO;
    float cut = opts->cut_ratio ? opts->cut_ratio : MAGNET_CUT_RATIO;
    float gap = opts->gap_ratio ? opts->gap_ratio : MAGNET_GAP_RATIO;
    float taper = opts->taper ? opts->taper : MAGNET_TAPER;

    if (bar <= 0.0f || bar >= 1.0f || taper < 0.0f || taper >= 45.0f)   return CERR_INVALID_ARGUMENTS;

    /* the ring radius, from the total width */
    float R, pole;
    if (opts->pole_len > 0.0f) {
        pole = opts->pole_len;
        R = (opts->width - pole) / (1.0f + cut + gap);
    } else {
        float pole_ratio = opts->pole_ratio ? opts->pole_ratio : MAGNET_POLE_RATIO;
        R = magnet_radius(opts->width, cut, gap, pole_ratio);
        pole = pole_ratio * R;
    }

    if (R <= 0.0f || pole <= 0.0f)  return CERR_INVALID_ARGUMENTS;

    float r = R * (1.0f - bar);
    float tau = to_radians(taper);
    float phi = M_PI_2 - tau;       /* where the arms leave the circles */
    float x_cut = cut * R;
    float x0 = x_cut + gap * R;     /* detached pole X extent */
    float x1 = x0 + pole;

    /*
     * The arm bars run between the lines tangent to the outer/inner
     * circles at @phi, converging toward the axis at @tau
     */
    float slope = tanf(tau);
    float out_x = R * cosf(phi), out_y = R * sinf(phi);
    float in_x = r * cosf(phi), in_y = r * sinf(phi);
#define OUTER_Y(_x) (out_y - slope * ((_x) - out_x))
#define INNER_Y(_x) (in_y - slope * ((_x) - in_x))

    /* the cut must be past the tangent points, the poles must not cross the axis */
    if (x_cut <= out_x || INNER_Y(x1) <= 0.0f)  return CERR_INVALID_ARGUMENTS;

    unsigned int nseg = opts->nr_segments ? opts->nr_segments : MAGNET_SEGMENTS;
    float span = 2.0f * (M_PI - phi);
    unsigned int n = clamp((int)ceilf(span / (2.0f * M_PI) * nseg), 8, 512);

    float tip_round = opts->round_ratio ? opts->round_ratio : MAGNET_ROUND_RATIO;
    float round_out = tip_round * R, round_in = round_out / 2.0f;
    unsigned int arc_seg = clamp(nseg / 8, 3, 16);

    /*
     * body ring: outer arc + cut + inner arc + cut, plus two 4-corner
     * poles with two rounded tip corners each
     */
    size_t nr_body = 2 * (n + 1) + 4;
    size_t nr_pole_max = 2 + 2 * (arc_seg + 1);
    vec2 *points = mem_alloc(sizeof(*points), .nr = nr_body + 2 * nr_pole_max);
    if (!points)    return CERR_NOMEM;

    size_t w = 0;
    /* outer arc, counterclockwise from the upper tangent point */
    for (unsigned int i = 0; i <= n; i++) {
        float a = phi + span * i / n;

        points[w][0] = R * cosf(a);
        points[w][1] = R * sinf(a);
        w++;
    }

    /* lower arm: along the tangent line to the cut, then up the cut */
    points[w][0] = x_cut; points[w][1] = -OUTER_Y(x_cut); w++;
    points[w][0] = x_cut; points[w][1] = -INNER_Y(x_cut); w++;

    /* inner boundary: back along the lower tangent line, the arc, out the upper line */
    for (unsigned int i = 0; i <= n; i++) {
        float a = (2.0f * M_PI - phi) - span * i / n;

        points[w][0] = r * cosf(a);
        points[w][1] = r * sinf(a);
        w++;
    }

    points[w][0] = x_cut; points[w][1] = INNER_Y(x_cut); w++;
    points[w][0] = x_cut; points[w][1] = OUTER_Y(x_cut); w++;

    /* detached poles: bar continuations after the air gap, tip corners rounded */
    vec2 up_corners[4] = {
        { x0, INNER_Y(x0) }, { x1, INNER_Y(x1) }, { x1, OUTER_Y(x1) }, { x0, OUTER_Y(x0) },
    };
    float up_radius[4] = { 0.0f, round_in, round_out, 0.0f };

    size_t pole_up = w;
    size_t nr_pole_up = poly_fillet(&points[w], up_corners, up_radius, 4, arc_seg);
    w += nr_pole_up;

    vec2 down_corners[4] = {
        { x0, -OUTER_Y(x0) }, { x1, -OUTER_Y(x1) }, { x1, -INNER_Y(x1) }, { x0, -INNER_Y(x0) },
    };
    float down_radius[4] = { 0.0f, round_out, round_in, 0.0f };

    size_t pole_down = w;
    size_t nr_pole_down = poly_fillet(&points[w], down_corners, down_radius, 4, arc_seg);
    w += nr_pole_down;

    for (size_t i = 0; i < w; i++) {
        points[i][0] += opts->org[0];
        points[i][1] += opts->org[1];
    }

    poly_ring rings[] = {
        { .start = 0,         .nr = nr_body },
        { .start = pole_up,   .nr = nr_pole_up },
        { .start = pole_down, .nr = nr_pole_down },
    };

    prim_extrusion ext = {
        .points     = points,
        .rings      = rings,
        .nr_rings   = array_size(rings),
        .z          = opts->org[2],
        .depth      = opts->depth,
        .closed     = opts->closed,
    };

    cerr err = _prim_emit_extrusion(&ext, &(const prim_emit_opts){ .mesh = opts->mesh });

    mem_free(points);

    return err;
}

cerr _logo_emit(const logo_emit_opts *opts)
{
    if (!opts || !opts->mesh || !opts->font || opts->width <= 0.0f) return CERR_INVALID_ARGUMENTS;

    const char *text = opts->text ? opts->text : "LAP";
    const char *tagline = opts->tagline ? opts->tagline : "world engine";

    vec4 text_ext;
    CERR_RET_CERR(text_mesh_extents(opts->font, text, 0, text_ext));

    float cap_em = text_ext[3];
    float text_w_em = text_ext[2] - text_ext[0];
    if (cap_em <= 0.0f || text_w_em <= 0.0f)    return CERR_INVALID_ARGUMENTS;

    /*
     * The lettering's right edge, in ring radii: the detached poles
     * stretch to end flush with it
     */
    float right = LOGO_TEXT_X_RATIO + LOGO_CAP_RATIO * text_w_em / cap_em;
    float pole_ratio = right - MAGNET_CUT_RATIO - MAGNET_GAP_RATIO;

    /* keep a stub of a pole under a magnet-dwarfing lettering */
    if (pole_ratio < 0.1f)  pole_ratio = 0.1f;

    float R = magnet_radius(opts->width, MAGNET_CUT_RATIO, MAGNET_GAP_RATIO, pole_ratio);

    CERR_RET_CERR(_magnet_emit(&(magnet_emit_opts){
        .mesh       = opts->mesh,
        .org        = { opts->org[0], opts->org[1], opts->org[2] },
        .width      = opts->width,
        .depth      = opts->depth,
        .taper      = opts->taper,
        .pole_ratio = pole_ratio,
        .closed     = opts->closed,
    }));

    /* the lettering, vertically centered in the magnet's mouth */
    float size = LOGO_CAP_RATIO * R / cap_em;
    CERR_RET_CERR(_text_mesh_emit_string(opts->font, text, &(text_mesh_opts){
        .mesh   = opts->mesh,
        .org    = {
            opts->org[0] + LOGO_TEXT_X_RATIO * R - size * text_ext[0],
            opts->org[1] - LOGO_CAP_RATIO * R / 2.0f,
            opts->org[2],
        },
        .size   = size,
        .depth  = opts->depth,
        .closed = opts->closed,
    }));

    if (!opts->tagline_font || !tagline[0])     return CERR_OK;

    vec4 tag_ext;
    CERR_RET_CERR(text_mesh_extents(opts->tagline_font, tagline, 0, tag_ext));

    float tag_w_em = tag_ext[2] - tag_ext[0];
    if (tag_w_em <= 0.0f)   return CERR_OK;

    /* the tagline, centered under the whole thing */
    float tag_size = LOGO_TAG_W_RATIO * opts->width / tag_w_em;
    float center_x = opts->org[0] + (right * R - R) / 2.0f;

    return _text_mesh_emit_string(opts->tagline_font, tagline, &(text_mesh_opts){
        .mesh   = opts->mesh,
        .org    = {
            center_x - tag_size * (tag_ext[2] + tag_ext[0]) / 2.0f,
            opts->org[1] - LOGO_TAG_Y_RATIO * R,
            opts->org[2],
        },
        .size   = tag_size,
        .depth  = opts->depth,
        .closed = opts->closed,
    });
}
