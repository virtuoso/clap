/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_WINDOW_WALL_H__
#define __CLAP_PCG_WINDOW_WALL_H__

#include "error.h"
#include "linmath.h"
#include "pcg/glazing.h"
#include "pcg/sink.h"
#include "pcg/span.h"
#include "pcg/surface.h"

/**
 * enum pcg_window_wall_surf - pcg_window_wall_params surfaces
 * @PCG_WINDOW_WALL_SPANDREL:   the solid band below the sill
 * @PCG_WINDOW_WALL_HEADER:     the solid band above the head
 * @PCG_WINDOW_WALL_POST:       the posts at the ends, on the bay lines and
 *                              either side of every opening
 * @PCG_WINDOW_WALL_MULLION:    the mullions that split each bay's window
 * @PCG_WINDOW_WALL_NR_SURF:    number of surfaces
 *
 * The glass is the embedded pcg_glazing_params' surfaces.
 */
enum pcg_window_wall_surf {
    PCG_WINDOW_WALL_SPANDREL,
    PCG_WINDOW_WALL_HEADER,
    PCG_WINDOW_WALL_POST,
    PCG_WINDOW_WALL_MULLION,
    PCG_WINDOW_WALL_NR_SURF,
};

/**
 * struct pcg_window_wall_params - a window wall's style
 * @glazing:    the band's glass, in a pcg_glazing_emit() over the band;
 *              its members are skipped, as the posts and mullions frame
 *              the panes
 * @surf:       per-surface finish, also by name
 * @t:          thickness
 * @sill:       top of the spandrel, the window band's bottom
 * @head:       bottom of the header, the window band's top
 * @bay:        target bay width: the wall takes the whole number of
 *              bays nearest its length / @bay, at least one
 * @post_w:     post width along the wall
 * @mull_w:     mullion width along the wall
 * @mull_pitch: target mullion spacing: each bay takes the whole number
 *              of panes nearest its width / @mull_pitch, at least one
 * @open_below: a storefront: no spandrel, the posts, mullions and glass
 *              run down to the floor
 *
 * A wall of a spandrel up to @sill and a header from @head up, with a
 * glazed band between them divided into bays by posts, and every bay
 * into panes by mullions. Everything solid is a box centered on the
 * wall's line, charting the frame's local meters, and the glass lies on
 * the line. An opening is a portal from the floor up: it takes its width
 * out of the spandrel and the glass and gets a post either side, rising
 * to the header, which carries on over it; bay posts and mullions that
 * would touch those posts are left out. Its own head lies in the band
 * and goes to the glazing, so a head below the band's leaves a transom
 * pane over the portal.
 */
typedef struct pcg_window_wall_params {
    pcg_glazing_params  glazing;
    union {
        pcg_surface surf[PCG_WINDOW_WALL_NR_SURF];
        struct {
            pcg_surface spandrel, header, post, mullion;
        };
    };
    float               t;
    float               sill;
    float               head;
    float               bay;
    float               post_w;
    float               mull_w;
    float               mull_pitch;
    bool                open_below;
} pcg_window_wall_params;

/**
 * pcg_window_wall_params_init() - a generic window wall's style
 * @p:  the params to fill, every field
 *
 * A 0.22 m office facade with a window band from 0.95 to 3.05 m, bays
 * near 4.8 m between 0.3 m posts and panes near 1.2 m between 0.09 m
 * mullions, over a spandrel. The spandrel, header and posts are
 * PCG_ROLE_CLADDING, the mullions PCG_ROLE_METAL, all PCG_COLLIDE_SOLID;
 * the glazing is pcg_glazing_params_init()'s, with its members skipped.
 */
void pcg_window_wall_params_init(pcg_window_wall_params *p);

/**
 * struct pcg_window_wall_site - where a window wall goes
 * @frame:          the wall's line on the floor, +a along it; the wall
 *                  straddles it
 * @len:            wall length; the wall runs over [0, @len]
 * @h:              wall height, above the head
 * @openings:       portals, such as an entrance, centered at their @at
 *                  and in order along the wall (see pcg_openings_check()),
 *                  each with its head in the window band: above the
 *                  sill (the floor, if open below) and no higher than
 *                  the band's head, as the header always carries on over
 *                  a portal; nullptr: none
 * @nr_openings:    number of @openings
 */
typedef struct pcg_window_wall_site {
    pcg_frame           frame;
    float               len;
    float               h;
    const pcg_opening   *openings;
    size_t              nr_openings;
} pcg_window_wall_site;

/**
 * pcg_window_wall_check() - check a window wall's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Everything _pcg_window_wall_emit() checks before it emits anything,
 * the glazing's checks of the window band included. A parent that places
 * several children makes the checks for all of them before any of them
 * emits, so that a bad one leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a dimension, length or height that
 * isn't finite, a dimension that isn't positive, a head not above the
 * sill, a wall not taller than the head or shorter than its two end
 * posts, openings that don't fit (pcg_openings_check()), leave no room
 * for their posts between the end posts and each other or have their
 * heads outside the window band, or bays or panes no wider than the
 * posts and mullions around them; CERR_TOO_LARGE for more bays or panes
 * than an int counts; the pcg_glazing_check() errors
 */
cerr_check pcg_window_wall_check(const pcg_window_wall_params *p, const pcg_window_wall_site *site);

/**
 * _pcg_window_wall_emit() - emit a window wall
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_window_wall_check(), pcg_glazing_emit() and pcg_box() errors
 */
cerr_check _pcg_window_wall_emit(const pcg_ctx *ctx, const pcg_window_wall_params *p,
                                 const pcg_window_wall_site *site);

/**
 * define pcg_window_wall_emit - emit a window wall at a site described by pcg_window_wall_site initializers
 *
 * Syntax sugar for _pcg_window_wall_emit().
 */
#define pcg_window_wall_emit(_ctx, _p, args...) \
    _pcg_window_wall_emit((_ctx), (_p), &(const pcg_window_wall_site){ args })

#endif /* __CLAP_PCG_WINDOW_WALL_H__ */
