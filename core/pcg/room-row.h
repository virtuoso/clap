/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_ROOM_ROW_H__
#define __CLAP_PCG_ROOM_ROW_H__

#include "error.h"
#include "linmath.h"
#include "pcg/room.h"
#include "pcg/sink.h"

/** PCG_ROOM_LABEL_MAX - the longest sign text a label callback can write, terminator included */
#define PCG_ROOM_LABEL_MAX  32

typedef struct pcg_room_entry pcg_room_entry;

/**
 * typedef pcg_room_fn - pick one room of a row
 * @tag:    the room's place: the row's floor, segment and ring, and the
 *          room's number
 * @priv:   pcg_room_entry::priv
 * @room:   seeded with the entry; change any of it, but @room->fn and
 *          @room->priv, which are ignored
 * @p:      seeded with the row's room style; change any of it, the
 *          surfaces included, for this room alone
 *
 * The plug-in point for rooms that vary along a row or from row to row:
 * called for every room the entry lays out, before the row lays any of
 * them out, every time the row is laid out: to emit it, and again for
 * pcg_room_ring_check() when a parent checks a ring before it emits it.
 * It must pick the same room for the same @tag and @priv every time:
 * state kept in @priv that changes from call to call leaves the rooms
 * emitted other than the rooms checked. A pick that varies keys off @tag.
 */
typedef void (*pcg_room_fn)(const pcg_room_tag *tag, void *priv, pcg_room_entry *room, pcg_room_params *p);

/**
 * struct pcg_room_entry - one room of a row
 * @fn:         picks the room's values where it is laid out; nullptr:
 *              the entry's own
 * @priv:       passed to @fn
 * @width:      width along the row, walls included
 * @depth:      depth into the row, walls included
 * @wall_mask:  the room's walls and doorways, see pcg_room_site
 * @width_frac: @width is a fraction of the row's length
 * @depth_frac: @depth is a fraction of the row site's depth_ref
 */
struct pcg_room_entry {
    pcg_room_fn fn;
    void        *priv;
    float       width;
    float       depth;
    uint32_t    wall_mask;
    bool        width_frac;
    bool        depth_frac;
};

/**
 * typedef pcg_room_label_fn - a room's sign text
 * @tag:    the room's place and number
 * @priv:   pcg_room_row_params::label_priv
 * @buf:    the text, empty to begin with; leave it empty for no sign
 * @size:   size of @buf, PCG_ROOM_LABEL_MAX
 *
 * Called for every room with a front wall, every time the row is laid
 * out (see pcg_room_fn); it must write the same text for the same @tag
 * every time.
 */
typedef void (*pcg_room_label_fn)(const pcg_room_tag *tag, void *priv, char *buf, size_t size);

/**
 * struct pcg_room_row_params - a row of rooms' style
 * @room:       every room's style, before its entry's fn changes it
 * @label:      sign text callback; nullptr: no signs
 * @label_priv: passed to @label
 *
 * A strip of rooms along a line: the entries side by side in their order
 * along +a, the strip centered on the line, every room standing with its
 * back on the line and its depth along +c (see pcg_room_site). Rooms are
 * numbered in their order from the site's first number on, and each one
 * is a pcg_room_emit() of its own, flank walls and all: neighbours stand
 * back to back.
 */
typedef struct pcg_room_row_params {
    pcg_room_params     room;
    pcg_room_label_fn   label;
    void                *label_priv;
} pcg_room_row_params;

/**
 * pcg_room_row_params_init() - a generic row's style
 * @p:  the params to fill, every field
 *
 * pcg_room_params_init()'s rooms, without signs.
 */
void pcg_room_row_params_init(pcg_room_row_params *p);

/**
 * struct pcg_room_row_site - where a row of rooms goes
 * @frame:      the row's line at floor level: its start, +a along it, +c
 *              into the rooms
 * @len:        the line's length; the strip is centered on [0, @len]
 * @depth_ref:  the length a depth given as a fraction is a fraction of
 * @h:          wall height
 * @ceiling:    height of the ceiling the lights hang from
 * @rooms:      the rooms, in order along +a
 * @nr_rooms:   number of @rooms
 * @tag:        the row's place in its building, and the number of its
 *              first room
 */
typedef struct pcg_room_row_site {
    pcg_frame               frame;
    float                   len;
    float                   depth_ref;
    float                   h;
    float                   ceiling;
    const pcg_room_entry    *rooms;
    size_t                  nr_rooms;
    pcg_room_tag            tag;
} pcg_room_row_site;

/**
 * _pcg_room_row_emit() - emit a row of rooms
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Picks every room (pcg_room_entry::fn), lays them out and checks them
 * all (pcg_room_check()) before any of them is emitted. The checks cover
 * the rooms' measures and fits, not their surfaces: a surface the sink
 * can't resolve against @ctx's palette, like a room's visit hook that
 * fails, leaves the rooms before it behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @ctx, @p or
 * @site, a frame pcg_frame_check() turns down, a length that isn't
 * positive and finite, a depth_ref that isn't finite, rooms missing, a
 * room's width or depth that isn't positive and finite as given or as it
 * comes out, or rooms wider in all than the line, past their widths'
 * rounding; the pcg_room_check() errors of any room; CERR_NOMEM; the
 * pcg_room_emit() errors
 */
cerr_check _pcg_room_row_emit(const pcg_ctx *ctx, const pcg_room_row_params *p, const pcg_room_row_site *site);

/**
 * define pcg_room_row_emit - emit a row of rooms at a site described by pcg_room_row_site initializers
 *
 * Syntax sugar for _pcg_room_row_emit().
 */
#define pcg_room_row_emit(_ctx, _p, args...) \
    _pcg_room_row_emit((_ctx), (_p), &(const pcg_room_row_site){ args })

/**
 * struct pcg_room_ring_site - where a ring of rooms goes
 * @frame:      the ring's frame; its y is the floor
 * @pts:        the rows' lines, a closed polygon in the frame's (a, c):
 *              a row along every side, from its point to the next one,
 *              with the rooms to the left; wound counterclockwise, they
 *              face into it
 * @nr_pts:     number of points, and of sides
 * @rooms:      the row along every side
 * @nr_rooms:   number of @rooms
 * @skip:       sides left without a row, such as an entrance; nullptr:
 *              none
 * @nr_skip:    number of @skip
 * @depth_ref:  as in pcg_room_row_site
 * @h:          wall height
 * @ceiling:    height of the ceiling the lights hang from
 * @tag:        the ring's place in its building, and the number of its
 *              first room; the segment is each row's side
 *
 * The rows are numbered on from one another, in the order of the sides:
 * a ring numbers @nr_rooms rooms for every side it doesn't skip.
 */
typedef struct pcg_room_ring_site {
    pcg_frame               frame;
    const vec2              *pts;
    size_t                  nr_pts;
    const pcg_room_entry    *rooms;
    size_t                  nr_rooms;
    const size_t            *skip;
    size_t                  nr_skip;
    float                   depth_ref;
    float                   h;
    float                   ceiling;
    pcg_room_tag            tag;
} pcg_room_ring_site;

/**
 * pcg_room_ring_check() - check a ring of rooms' style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Picks, lays out and checks every room of every row, as
 * _pcg_room_ring_emit() does before it emits any of them, calling the
 * entries' fns and the label fn on the way. A parent that places a ring
 * among other children makes the checks before any of its children
 * emits, so that a ring that doesn't fit leaves nothing behind; the
 * rooms' surfaces are only resolved when they are emitted.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site and
 * for what _pcg_room_ring_emit() turns down in them; CERR_TOO_LARGE for
 * more rooms in all the rows than a size_t counts; CERR_NOMEM
 */
cerr_check pcg_room_ring_check(const pcg_room_row_params *p, const pcg_room_ring_site *site);

/**
 * _pcg_room_ring_emit() - emit a ring of rooms
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Picks, lays out and checks every room of every row before any of them
 * is emitted, as _pcg_room_row_emit() does for one row, surfaces and
 * visit hooks failing late the same way.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @ctx, @p or
 * @site, a frame pcg_frame_check() turns down, a polygon of fewer than 3
 * points, missing or with a point that isn't finite, consecutive points
 * that coincide, or skipped sides missing or past the last one;
 * CERR_TOO_LARGE for more rooms in all the rows than a size_t counts;
 * CERR_NOMEM; the _pcg_room_row_emit() errors of any row
 */
cerr_check _pcg_room_ring_emit(const pcg_ctx *ctx, const pcg_room_row_params *p, const pcg_room_ring_site *site);

/**
 * define pcg_room_ring_emit - emit a ring of rooms at a site described by pcg_room_ring_site initializers
 *
 * Syntax sugar for _pcg_room_ring_emit().
 */
#define pcg_room_ring_emit(_ctx, _p, args...) \
    _pcg_room_ring_emit((_ctx), (_p), &(const pcg_room_ring_site){ args })

#endif /* __CLAP_PCG_ROOM_ROW_H__ */
