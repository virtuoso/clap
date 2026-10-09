/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_WALL_H__
#define __CLAP_PCG_WALL_H__

#include "error.h"
#include "linmath.h"
#include "pcg/sink.h"
#include "pcg/span.h"
#include "pcg/surface.h"

/**
 * enum pcg_wall_surf - pcg_wall_params surfaces
 * @PCG_WALL_FACE_L:    the face toward the frame's +c
 * @PCG_WALL_FACE_R:    the face toward the frame's -c
 * @PCG_WALL_REVEAL:    everything between the two faces: the ends, the
 *                      top, the bottom, and the jambs and heads of the
 *                      openings
 * @PCG_WALL_NR_SURF:   number of surfaces
 */
enum pcg_wall_surf {
    PCG_WALL_FACE_L,
    PCG_WALL_FACE_R,
    PCG_WALL_REVEAL,
    PCG_WALL_NR_SURF,
};

/**
 * struct pcg_wall_params - a solid wall's style
 * @surf:   per-surface finish, also by name
 * @t:      thickness
 *
 * A solid wall or partition standing on the floor, with doorways cut into
 * it. The wall stands on its frame's line and takes its thickness toward
 * +c: the -c face lies on the line, the +c face @t from it. An opening
 * takes its width out of the wall from the floor up to its head, and the
 * wall carries on over it as a header; an opening whose head isn't below
 * the top of the wall leaves it open all the way up. Openings get no
 * trim: their cut faces are the reveal. Every box charts the frame's
 * local meters, so walls along one line tile.
 */
typedef struct pcg_wall_params {
    union {
        pcg_surface surf[PCG_WALL_NR_SURF];
        struct {
            pcg_surface face_l, face_r, reveal;
        };
    };
    float   t;
} pcg_wall_params;

/**
 * pcg_wall_params_init() - a generic wall's style
 * @p:  the params to fill, every field
 *
 * A 0.09 m partition. Every surface is PCG_ROLE_CLADDING and
 * PCG_COLLIDE_SOLID.
 */
void pcg_wall_params_init(pcg_wall_params *p);

/**
 * struct pcg_wall_site - where a wall goes
 * @frame:          the wall's line on the floor, +a along it
 * @openings:       doorways, centered at their @at from the wall's start
 *                  and in order along it (see pcg_openings_check());
 *                  nullptr: none
 * @nr_openings:    number of @openings
 * @a0:             where the wall starts along +a: it runs over
 *                  [@a0, @a0 + @len]
 * @len:            wall length
 * @h:              wall height
 *
 * @a0 lets walls that start at different places along one line, such as
 * the walls of a row of rooms, keep one texture grid.
 */
typedef struct pcg_wall_site {
    pcg_frame           frame;
    const pcg_opening   *openings;
    size_t              nr_openings;
    float               a0;
    float               len;
    float               h;
} pcg_wall_site;

/**
 * pcg_wall_check() - check a wall's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Everything _pcg_wall_emit() checks before it emits anything. A parent
 * that places several children makes the checks for all of them before
 * any of them emits, so that a bad one leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a thickness, length or height that
 * isn't positive and finite, a start that isn't finite, openings that
 * don't fit (pcg_openings_check()) or an opening head that isn't finite
 */
cerr_check pcg_wall_check(const pcg_wall_params *p, const pcg_wall_site *site);

/**
 * _pcg_wall_emit() - emit a wall
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_wall_check() and pcg_box() errors
 */
cerr_check _pcg_wall_emit(const pcg_ctx *ctx, const pcg_wall_params *p, const pcg_wall_site *site);

/**
 * define pcg_wall_emit - emit a wall at a site described by pcg_wall_site initializers
 *
 * Syntax sugar for _pcg_wall_emit().
 */
#define pcg_wall_emit(_ctx, _p, args...) _pcg_wall_emit((_ctx), (_p), &(const pcg_wall_site){ args })

#endif /* __CLAP_PCG_WALL_H__ */
