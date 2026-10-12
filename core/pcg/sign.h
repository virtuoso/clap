/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_SIGN_H__
#define __CLAP_PCG_SIGN_H__

#include "error.h"
#include "pcg/sink.h"
#include "pcg/span.h"
#include "pcg/surface.h"

/**
 * enum pcg_sign_surf - pcg_sign_params surfaces
 * @PCG_SIGN_PLATE:     the plate and its lettering
 * @PCG_SIGN_NR_SURF:   number of surfaces
 */
enum pcg_sign_surf {
    PCG_SIGN_PLATE,
    PCG_SIGN_NR_SURF,
};

/**
 * struct pcg_sign_params - a wall plaque's style
 * @surf:           per-surface finish, also by name; the plate's
 *                  material takes PCG_UV_UNIT coordinates and letters it
 * @h:              plate height; the plate is as wide as its text needs
 * @center_y:       height of the plate's center over the floor, at least
 *                  half of @h
 * @standoff:       from a solid face out to the plate
 * @glass_standoff: from a pane out to the plate, clear of the members
 *                  that frame the pane
 * @door_offset:    from a doorway's far jamb along the wall to the
 *                  plate's center
 *
 * A text plate on a wall, reading along it from the side the face looks
 * toward: centered on the wall's run, or beside its doorway, past the
 * jamb farther along the run, rather than over it. The plate is a label
 * (pcg_sink_label()): the text is lettered when the sink is committed,
 * into an atlas shared by the plates of a chunk that wear the same
 * material and collide the same way.
 */
typedef struct pcg_sign_params {
    union {
        pcg_surface surf[PCG_SIGN_NR_SURF];
        struct {
            pcg_surface plate;
        };
    };
    float   h;
    float   center_y;
    float   standoff;
    float   glass_standoff;
    float   door_offset;
} pcg_sign_params;

/**
 * pcg_sign_params_init() - a room plaque's style
 * @p:  the params to fill, every field
 *
 * A plate 0.32 m high, centered 1.5 m over the floor, 0.02 m off a solid
 * face or 0.05 m off a pane, 0.30 m past a doorway. The plate is
 * PCG_ROLE_SIGN and PCG_COLLIDE_NONE.
 */
void pcg_sign_params_init(pcg_sign_params *p);

/**
 * struct pcg_sign_site - where a sign goes
 * @frame:  the line of the face the sign hangs on, at floor level: +a
 *          along the wall, +c out of the face toward the reader
 * @a0:     where the wall's run starts along +a: it runs over
 *          [@a0, @a0 + @len]
 * @len:    the run's length
 * @door:   a doorway in the run, centered at its at from the run's
 *          start; nullptr: none
 * @text:   the sign's text, copied
 * @glass:  the face is a pane: stand off by the glass standoff
 *
 * @a0 lets the signs of a row of rooms take the frame of the line their
 * walls stand on, as the walls do.
 */
typedef struct pcg_sign_site {
    pcg_frame           frame;
    float               a0;
    float               len;
    const pcg_opening   *door;
    const char          *text;
    bool                glass;
} pcg_sign_site;

/**
 * pcg_sign_check() - check a sign's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Everything _pcg_sign_emit() checks before it records anything. A
 * parent that places several children makes the checks for all of them
 * before any of them emits, so that a bad one leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a dimension, start or length that
 * isn't finite, a height that isn't positive, a center below half of it,
 * a negative standoff or door offset, a run without length, a doorway
 * that doesn't fit the run, a sign whose center falls past the run's end,
 * or no text
 */
cerr_check pcg_sign_check(const pcg_sign_params *p, const pcg_sign_site *site);

/**
 * _pcg_sign_emit() - record a sign
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_sign_check() and pcg_sink_label() errors
 */
cerr_check _pcg_sign_emit(const pcg_ctx *ctx, const pcg_sign_params *p, const pcg_sign_site *site);

/**
 * define pcg_sign_emit - record a sign at a site described by pcg_sign_site initializers
 *
 * Syntax sugar for _pcg_sign_emit().
 */
#define pcg_sign_emit(_ctx, _p, args...) _pcg_sign_emit((_ctx), (_p), &(const pcg_sign_site){ args })

#endif /* __CLAP_PCG_SIGN_H__ */
