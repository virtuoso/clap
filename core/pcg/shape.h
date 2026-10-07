/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_SHAPE_H__
#define __CLAP_PCG_SHAPE_H__

#include <limits.h>
#include "error.h"
#include "linmath.h"
#include "triangulate.h"
#include "pcg/sink.h"
#include "pcg/span.h"
#include "pcg/surface.h"

/*
 * Shapes are the bottom layer of the generators: the only code that
 * writes vertices. Each takes a description in its context's local
 * coordinates, routes every face group through pcg_sink_reserve() to its
 * surface's bucket and bakes the frame into the vertices; under a
 * mirrored frame the faces are wound the other way, so they keep
 * pointing out. Texture coordinates are in meters (PCG_UV_METERS): a
 * material's UV factor sets the density, so swapping a finish never
 * re-emits geometry.
 */

/**
 * enum pcg_plate_surf - pcg_plate_desc surfaces
 * @PCG_PLATE_TOP:      the top face, at y1, facing up
 * @PCG_PLATE_BOTTOM:   the bottom face, at y0, facing down
 * @PCG_PLATE_EDGE:     the side faces around every ring
 * @PCG_PLATE_NR_SURF:  number of surfaces
 */
enum pcg_plate_surf {
    PCG_PLATE_TOP,
    PCG_PLATE_BOTTOM,
    PCG_PLATE_EDGE,
    PCG_PLATE_NR_SURF,
};

/**
 * struct pcg_plate_desc - a horizontal plate
 * @pts:        polygon points in the context's local (a, c)
 * @nr_pts:     number of points
 * @rings:      fill boundaries and holes in any order and winding (see
 *              poly_triangulate()); nullptr: one ring of all of @pts
 * @nr_rings:   number of @rings
 * @y0:         bottom height
 * @y1:         top height; equal to @y0 for a sheet without edges, which
 *              shows the side whose surface isn't skipped
 * @surf:       per-surface finish, also by name
 * @fan:        fan-triangulate from point 0 instead of ear clipping; for
 *              a convex outline without holes
 *
 * Top and bottom texture coordinates are the sink-space (x, z) in
 * meters, so plates that wear the same material tile seamlessly across
 * each other, whichever element emitted them. Edge faces run U along
 * each ring edge from 0 to its length and V up the local y. Every ring
 * gets edges wound to face away from the plate's body: out of a fill
 * ring, into a hole.
 */
typedef struct pcg_plate_desc {
    const vec2      *pts;
    size_t          nr_pts;
    const poly_ring *rings;
    size_t          nr_rings;
    float           y0;
    float           y1;
    union {
        pcg_surface surf[PCG_PLATE_NR_SURF];
        struct {
            pcg_surface top, bottom, edge;
        };
    };
    bool            fan;
} pcg_plate_desc;

/**
 * _pcg_plate() - emit a horizontal plate
 * @ctx:    where it goes
 * @d:      the plate
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for fewer than 3 points, a
 * ring outside @d->pts, a fan over holes or y0 above y1; the
 * poly_triangulate() and pcg_sink_reserve() errors
 */
cerr_check _pcg_plate(const pcg_ctx *ctx, const pcg_plate_desc *d);

/**
 * define pcg_plate - emit a plate described by pcg_plate_desc initializers
 *
 * Syntax sugar for _pcg_plate().
 */
#define pcg_plate(_ctx, args...) _pcg_plate((_ctx), &(const pcg_plate_desc){ args })

/**
 * struct pcg_band_desc - a box run along +a with a band chart
 * @lo:         one corner in the context's local (a, y, c)
 * @hi:         the opposite corner; a range reversed against @lo is
 *              swapped back, on every axis
 * @surf:       finish
 * @skip_mask:  &enum prim_box_face bits of the faces to leave out, the
 *              ones buried in a neighbour
 *
 * The building block of drop bands and fascias: what reads is the long
 * faces, so their charts follow the run. The -c face runs U along +a
 * over [a0, a1], the +c face along -a over the same range, and both run
 * V up over [y0, y1]. The top runs U along +a over [a0, a1] and V along
 * +c over [0, c1 - c0]. The bottom and the end caps are transposed and
 * not to scale: the bottom runs U across c over [a0, a1] and V along +a
 * over [0, c1 - c0], and the end caps run U up over [0, c1 - c0] and V
 * across c over [y0, y1]. Consecutive bands along a run tile along it.
 */
typedef struct pcg_band_desc {
    vec3            lo;
    vec3            hi;
    pcg_surface     surf;
    unsigned int    skip_mask;
} pcg_band_desc;

/**
 * _pcg_band() - emit a band
 * @ctx:    where it goes
 * @d:      the band
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for an empty range on any axis;
 * the pcg_sink_reserve() errors
 */
cerr_check _pcg_band(const pcg_ctx *ctx, const pcg_band_desc *d);

/**
 * define pcg_band - emit a band described by pcg_band_desc initializers
 *
 * Syntax sugar for _pcg_band().
 */
#define pcg_band(_ctx, args...) _pcg_band((_ctx), &(const pcg_band_desc){ args })

/**
 * struct pcg_box_desc - a box with a surface per face
 * @lo:     one corner in the context's local (a, y, c)
 * @hi:     the opposite corner; a range reversed against @lo is swapped
 *          back, on every axis
 * @face:   the surface of each face, indexed by the bit number of its
 *          &enum prim_box_face flag; nullptr leaves the face out
 *
 * The box of prim_emit_box(), with its planar charts in the context's
 * local meters, heights included: a box placed at a storey wears its
 * texture the way it does on the ground. Per face:
 *  - +a end:   U +c, V +y
 *  - -a end:   U -c, V +y
 *  - top:      U +a, V +c
 *  - bottom:   U +c, V +a
 *  - +c side:  U -a, V +y
 *  - -c side:  U +a, V +y
 * Coplanar faces of neighbouring boxes in one frame tile seamlessly. The
 * faces that point to one surface share a reservation, and come in the
 * order of their flags.
 */
typedef struct pcg_box_desc {
    vec3                lo;
    vec3                hi;
    const pcg_surface   *face[6];
} pcg_box_desc;

/* pcg_box_desc::face initializer: every face on @_surf */
#define PCG_BOX_ALL(_surf) { (_surf), (_surf), (_surf), (_surf), (_surf), (_surf) }

/**
 * _pcg_box() - emit a box
 * @ctx:    where it goes
 * @d:      the box
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for an empty range on any axis;
 * the pcg_sink_reserve() errors
 */
cerr_check _pcg_box(const pcg_ctx *ctx, const pcg_box_desc *d);

/**
 * define pcg_box - emit a box described by pcg_box_desc initializers
 *
 * Syntax sugar for _pcg_box().
 */
#define pcg_box(_ctx, args...) _pcg_box((_ctx), &(const pcg_box_desc){ args })

/**
 * struct pcg_pane_desc - a double-sided pane in the local a-y plane
 * @open:       openings cut out of the pane, ordered along it (see
 *              pcg_openings_check()); a head between @y0 and @y1 leaves
 *              a transom pane over the opening
 * @nr_open:    number of openings
 * @len:        the pane runs over [0, @len] along +a, at c = 0
 * @y0:         bottom height
 * @y1:         top height
 * @min_seg:    pieces narrower than this along +a are left out; so are
 *              empty ones, even at 0
 * @surf:       finish
 *
 * Each piece is two quads wound against each other, so the pane shows,
 * and collides, from both sides. Texture coordinates run U along +a on
 * the -c side and along -a on the +c side, V up the local y.
 */
typedef struct pcg_pane_desc {
    const pcg_opening   *open;
    size_t              nr_open;
    float               len;
    float               y0;
    float               y1;
    float               min_seg;
    pcg_surface         surf;
} pcg_pane_desc;

/**
 * _pcg_pane() - emit a pane
 * @ctx:    where it goes
 * @d:      the pane
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a pane without length or
 * height, a negative @min_seg or openings that don't fit; the
 * pcg_sink_reserve() errors
 */
cerr_check _pcg_pane(const pcg_ctx *ctx, const pcg_pane_desc *d);

/**
 * define pcg_pane - emit a pane described by pcg_pane_desc initializers
 *
 * Syntax sugar for _pcg_pane().
 */
#define pcg_pane(_ctx, args...) _pcg_pane((_ctx), &(const pcg_pane_desc){ args })

/* shorter rod axes have no direction to build a cross-section around */
#define PCG_ROD_MIN_LEN 1e-5f

/* a rod takes 12 vertices per side, all in one mesh */
#define PCG_ROD_SIDES_MAX   (USHRT_MAX / 12)

/**
 * struct pcg_rod_desc - a capped n-gon prism between two points
 * @a:      near end center, in the context's local (a, y, c)
 * @b:      far end center
 * @ra:     circumradius at @a
 * @rb:     circumradius at @b
 * @phase:  cross-section rotation, radians; half a side turns a vertex
 *          into a flat
 * @sides:  number of sides, 3 to PCG_ROD_SIDES_MAX
 * @surf:   finish
 *
 * For members at any slope: sloped rails and struts. The cross-section
 * is laid out around the sink-space axis, as _prim_emit_prism() does,
 * except that an axis whose vertical component is above 0.9 (within
 * about 26 degrees of vertical) measures @phase from the frame's run
 * direction instead of world +x, so a leg turns with its frame's yaw.
 * The frame doesn't mirror the section: a rod's faces point out under
 * any frame. Side faces map U along the axis from 0 at @a to its length
 * at @b and V around the perimeter in meters at each end; the caps map
 * their own plane in meters around the end center.
 */
typedef struct pcg_rod_desc {
    vec3            a;
    vec3            b;
    float           ra;
    float           rb;
    float           phase;
    int             sides;
    pcg_surface     surf;
} pcg_rod_desc;

/**
 * _pcg_rod() - emit a rod
 * @ctx:    where it goes
 * @d:      the rod
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for fewer than 3 sides, a
 * radius that isn't positive or an axis shorter than PCG_ROD_MIN_LEN;
 * CERR_TOO_LARGE for more than PCG_ROD_SIDES_MAX sides; the
 * pcg_sink_reserve() errors
 */
cerr_check _pcg_rod(const pcg_ctx *ctx, const pcg_rod_desc *d);

/**
 * define pcg_rod - emit a rod described by pcg_rod_desc initializers
 *
 * Syntax sugar for _pcg_rod().
 */
#define pcg_rod(_ctx, args...) _pcg_rod((_ctx), &(const pcg_rod_desc){ args })

#endif /* __CLAP_PCG_SHAPE_H__ */
