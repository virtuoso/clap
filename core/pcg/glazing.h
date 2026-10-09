/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_GLAZING_H__
#define __CLAP_PCG_GLAZING_H__

#include "error.h"
#include "linmath.h"
#include "pcg/sink.h"
#include "pcg/span.h"
#include "pcg/surface.h"

/**
 * enum pcg_glazing_surf - pcg_glazing_params surfaces
 * @PCG_GLAZING_PANE:       the glass, both sides
 * @PCG_GLAZING_MEMBER:     the frame members around it, all faces
 * @PCG_GLAZING_NR_SURF:    number of surfaces
 */
enum pcg_glazing_surf {
    PCG_GLAZING_PANE,
    PCG_GLAZING_MEMBER,
    PCG_GLAZING_NR_SURF,
};

/**
 * struct pcg_glazing_params - a framed pane's style
 * @surf:       per-surface finish, also by name
 * @member_w:   member face width, in the pane's plane
 * @member_t:   member depth, across the pane
 * @min_seg:    pieces of the pane, and of the members along its bottom
 *              and top, narrower than this are left out; so are empty
 *              ones, even at 0
 * @sill:       a member runs along the bottom of the pane
 *
 * A double-sided pane (see pcg_pane_desc) in a frame of members centered
 * on it: verticals the full height at either end, and horizontals between
 * them along the top and, with @sill, the bottom. Members butt square,
 * the horizontals ending on the verticals. An opening cuts both the pane
 * and the sill, and gets a vertical jamb on either side; a head below
 * the top of the pane gets a head member over the opening, under a
 * transom pane, while an opening without one splits the top member too.
 * There is no door leaf. Every member charts the frame's local meters.
 */
typedef struct pcg_glazing_params {
    union {
        pcg_surface surf[PCG_GLAZING_NR_SURF];
        struct {
            pcg_surface pane, member;
        };
    };
    float   member_w;
    float   member_t;
    float   min_seg;
    bool    sill;
} pcg_glazing_params;

/**
 * pcg_glazing_params_init() - a generic glazing's style
 * @p:  the params to fill, every field
 *
 * 0.06 m wide members 0.05 m deep, with a sill; pieces under 0.01 m are
 * left out. The pane is PCG_ROLE_GLASS and PCG_COLLIDE_SOLID, the members
 * PCG_ROLE_METAL and PCG_COLLIDE_NONE: thin trim around a pane that
 * already collides.
 */
void pcg_glazing_params_init(pcg_glazing_params *p);

/**
 * struct pcg_glazing_site - where a framed pane goes
 * @frame:          the pane's line, +a along it; the pane lies in the
 *                  frame's a-y plane, at c = 0
 * @openings:       openings from @y0 up, centered at their @at and in
 *                  order along the pane (see pcg_openings_check());
 *                  nullptr: none
 * @nr_openings:    number of @openings
 * @len:            the pane runs over [0, @len] along +a
 * @y0:             bottom of the pane on the frame's local y
 * @y1:             top of the pane
 *
 * Heights are on the frame's local y rather than from a frame raised to
 * @y0, so panes at different heights in one frame keep one texture grid.
 */
typedef struct pcg_glazing_site {
    pcg_frame           frame;
    const pcg_opening   *openings;
    size_t              nr_openings;
    float               len;
    float               y0;
    float               y1;
} pcg_glazing_site;

/**
 * pcg_glazing_check() - check a framed pane's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Everything _pcg_glazing_emit() checks before it emits anything. A
 * parent that places several children makes the checks for all of them
 * before any of them emits, so that a bad one leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a dimension, length or height that
 * isn't finite, a member dimension that isn't positive, a negative
 * @min_seg, a pane without length or height, openings that don't fit
 * (pcg_openings_check()) or with a head that isn't finite, and, unless
 * the members are skipped, members that don't fit: a pane less than two
 * members long or high, openings without room for their jambs between
 * the ends and each other, or a head without room for its member under
 * the top member
 */
cerr_check pcg_glazing_check(const pcg_glazing_params *p, const pcg_glazing_site *site);

/**
 * _pcg_glazing_emit() - emit a framed pane
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_glazing_check(), pcg_pane() and pcg_box() errors
 */
cerr_check _pcg_glazing_emit(const pcg_ctx *ctx, const pcg_glazing_params *p, const pcg_glazing_site *site);

/**
 * define pcg_glazing_emit - emit a framed pane at a site described by pcg_glazing_site initializers
 *
 * Syntax sugar for _pcg_glazing_emit().
 */
#define pcg_glazing_emit(_ctx, _p, args...) _pcg_glazing_emit((_ctx), (_p), &(const pcg_glazing_site){ args })

#endif /* __CLAP_PCG_GLAZING_H__ */
