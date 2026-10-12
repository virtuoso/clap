/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_ROOM_H__
#define __CLAP_PCG_ROOM_H__

#include "error.h"
#include "pcg/fixture.h"
#include "pcg/glazing.h"
#include "pcg/sign.h"
#include "pcg/sink.h"
#include "pcg/surface.h"
#include "pcg/wall.h"

/**
 * enum pcg_room_side - the sides of a room, for its wall mask
 * @PCG_ROOM_FRONT:     the side across from the back, at the room's depth
 * @PCG_ROOM_BACK:      the side on the line the room stands on
 * @PCG_ROOM_LEFT:      the side at the room's start along the line
 * @PCG_ROOM_RIGHT:     the side at its end
 * @PCG_ROOM_NR_SIDES:  number of sides
 */
enum pcg_room_side {
    PCG_ROOM_FRONT = 0,
    PCG_ROOM_BACK,
    PCG_ROOM_LEFT,
    PCG_ROOM_RIGHT,
    PCG_ROOM_NR_SIDES,
};

/** PCG_ROOM_GLASS_BIT - where a side's glass wall bit starts: a solid wall's is the side's own */
#define PCG_ROOM_GLASS_BIT  4
/** PCG_ROOM_DOOR_BIT - where a side's doorway bit starts */
#define PCG_ROOM_DOOR_BIT   8

/** PCG_ROOM_SOLID() - the wall mask bit of a solid wall on @_side */
#define PCG_ROOM_SOLID(_side)   (1u << (_side))
/** PCG_ROOM_GLASS() - the wall mask bit of a glass wall on @_side */
#define PCG_ROOM_GLASS(_side)   (1u << ((_side) + PCG_ROOM_GLASS_BIT))
/** PCG_ROOM_DOOR() - the wall mask bit of a doorway in the wall on @_side */
#define PCG_ROOM_DOOR(_side)    (1u << ((_side) + PCG_ROOM_DOOR_BIT))
/** PCG_ROOM_MASK_ALL - every bit a wall mask may have */
#define PCG_ROOM_MASK_ALL       ((1u << (PCG_ROOM_NR_SIDES + PCG_ROOM_DOOR_BIT)) - 1)

/**
 * pcg_room_walled() - check a wall mask for a wall on a side
 * @mask:   the wall mask
 * @side:   &enum pcg_room_side
 *
 * Return: true if @side has a wall, solid or glass
 */
static inline bool pcg_room_walled(uint32_t mask, int side)
{
    return mask & (PCG_ROOM_SOLID(side) | PCG_ROOM_GLASS(side));
}

/**
 * enum pcg_room_surf - pcg_room_params surfaces
 * @PCG_ROOM_CARPET:    the carpet, all faces
 * @PCG_ROOM_NR_SURF:   number of surfaces
 *
 * The walls, the glass, the light and the sign wear their own params'
 * surfaces.
 */
enum pcg_room_surf {
    PCG_ROOM_CARPET,
    PCG_ROOM_NR_SURF,
};

/**
 * struct pcg_room_tag - where a room is in its building
 * @floor:      storey, 0 the ground floor
 * @segment:    the side of the polygon its row runs along
 * @ring:       the ring of rows it is in, 0 the outermost
 * @number:     its running number
 *
 * Handed on as it is to the callbacks that pick a room's content and
 * label it; the room itself reads none of it.
 */
typedef struct pcg_room_tag {
    int floor;
    int segment;
    int ring;
    int number;
} pcg_room_tag;

/**
 * struct pcg_room_info - a room as its visit hook sees it
 * @w:          interior width, along the visit context's +a
 * @d:          interior depth, along its +c, toward the front
 * @h:          clear height, from the floor to the ceiling
 * @door_w:     doorway width
 * @door_h:     doorway height
 * @wall_mask:  the room's walls and doorways (&enum pcg_room_side);
 *              every doorway is centered on its wall
 * @tag:        the room's place in its building
 *
 * The interior is [0, @w] x [0, @d] of the visit context's (a, c), on
 * its floor at y = 0: inside the walls' faces on every side, whether
 * the side has a wall or not.
 */
typedef struct pcg_room_info {
    float           w;
    float           d;
    float           h;
    float           door_w;
    float           door_h;
    uint32_t        wall_mask;
    pcg_room_tag    tag;
} pcg_room_info;

/**
 * typedef pcg_room_visit_fn - fill a room
 * @ctx:    the room's context: its sink, palette and chunk, with the
 *          frame at the interior's corner (see &struct pcg_room_info)
 * @info:   the room, in @ctx's frame
 * @priv:   pcg_room_params::visit_priv
 *
 * The plug-in point for content the room knows nothing about, such as
 * furniture: called once the room's own parts are emitted.
 *
 * Return: CERR_OK, or an error that aborts the build
 */
typedef cerr (*pcg_room_visit_fn)(const pcg_ctx *ctx, const pcg_room_info *info, void *priv);

/**
 * struct pcg_room_params - a room's style
 * @wall:       the solid walls; its thickness is every wall's, glass
 *              walls included
 * @glass:      the glass walls
 * @light:      the ceiling light
 * @sign:       the sign on the front
 * @visit:      called for every room once it is emitted; nullptr: none
 * @visit_priv: passed to @visit
 * @surf:       per-surface finish, also by name
 * @door_w:     doorway width
 * @door_h:     doorway height
 * @carpet_t:   carpet thickness
 *
 * An office: walls on the sides its wall mask names, solid or glass and
 * with or without a centered doorway, a carpet over its floor, a light
 * in the middle of its ceiling, a sign on its front, and then whatever
 * @visit puts in it. Every wall takes the wall's thickness inside the
 * room's outline. The walls on the left and the right own the corners:
 * they run the room's whole depth, and the front and the back stop at
 * their faces. A glass wall is a pane centered where a solid wall would
 * stand, in members, with the doorway's jambs and head around its
 * opening. The carpet spans the interior, inside where the walls would
 * stand on every side.
 */
typedef struct pcg_room_params {
    pcg_wall_params     wall;
    pcg_glazing_params  glass;
    pcg_fixture_params  light;
    pcg_sign_params     sign;
    pcg_room_visit_fn   visit;
    void                *visit_priv;
    union {
        pcg_surface surf[PCG_ROOM_NR_SURF];
        struct {
            pcg_surface carpet;
        };
    };
    float               door_w;
    float               door_h;
    float               carpet_t;
} pcg_room_params;

/**
 * pcg_room_params_init() - a generic office's style
 * @p:  the params to fill, every field
 *
 * 1.1 m by 2.2 m doorways and a 0.012 m carpet, PCG_ROLE_CARPET and
 * PCG_COLLIDE_NONE: a pile on a floor that already collides. The walls,
 * glass, light and sign are their params' defaults, and there is no
 * visit hook.
 */
void pcg_room_params_init(pcg_room_params *p);

/**
 * struct pcg_room_site - where a room goes
 * @frame:      the line the room's back stands on, at floor level: +a
 *              along it, +c into the room
 * @a0:         where the room starts along +a: it spans
 *              [@a0, @a0 + @w] x [0, @d] of the frame's (a, c)
 * @w:          width along +a, walls included
 * @d:          depth along +c, walls included
 * @h:          wall height; walls may run up past @ceiling, into what is
 *              above it
 * @ceiling:    height of the ceiling the light hangs from
 * @wall_mask:  which sides get walls: PCG_ROOM_SOLID(side) a solid one,
 *              PCG_ROOM_GLASS(side) a glass one (solid wins when both
 *              are set), PCG_ROOM_DOOR(side) a doorway in it
 * @label:      the sign's text, on the front wall, if there is one;
 *              nullptr or empty: no sign
 * @tag:        the room's place in its building, for its visit hook
 * @party_lo:   the left wall is a party wall a neighbour builds: leave
 *              it out, and lay the rest out as if it stood. The
 *              neighbour's wall stands where this one would: its
 *              outline overlaps this room's by the wall thickness.
 *              Rooms that only touch leave a slot a wall thick through
 *              the front, the back and the carpet
 * @party_hi:   the same for the right wall
 *
 * Rooms along one line keep one texture grid: @a0 places the room on
 * the line instead of a frame of its own, as on the wall site.
 */
typedef struct pcg_room_site {
    pcg_frame       frame;
    float           a0;
    float           w;
    float           d;
    float           h;
    float           ceiling;
    uint32_t        wall_mask;
    const char      *label;
    pcg_room_tag    tag;
    bool            party_lo;
    bool            party_hi;
} pcg_room_site;

/**
 * pcg_room_check() - check a room's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Everything _pcg_room_emit() checks before it emits anything: the
 * room's own style and site, and every wall, the light and the sign the
 * way their own checks check them at the sites the room gives them. A
 * parent that places several rooms makes the checks for all of them
 * before any of them emits, so that a bad one leaves nothing behind.
 * Children the room doesn't emit, such as the glass of a room without
 * glass walls, aren't checked.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a door or carpet dimension or a
 * wall thickness that isn't positive and finite, a start that isn't
 * finite, a width, depth, height or ceiling that isn't positive and
 * finite, a width or depth not over two walls' thickness, wall mask bits
 * past PCG_ROOM_MASK_ALL, or a flank doorway that runs into the front or
 * back wall; the pcg_wall_check(), pcg_glazing_check(),
 * pcg_fixture_check() and pcg_sign_check() errors
 */
cerr_check pcg_room_check(const pcg_room_params *p, const pcg_room_site *site);

/**
 * _pcg_room_emit() - emit a room
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Checks every part before any of them is emitted; then emits the walls,
 * the carpet, the light and the sign, and calls the visit hook last. A
 * hook that fails leaves the room's own parts behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_room_check() errors, the errors of the children's emits and the
 * visit hook's
 */
cerr_check _pcg_room_emit(const pcg_ctx *ctx, const pcg_room_params *p, const pcg_room_site *site);

/**
 * define pcg_room_emit - emit a room at a site described by pcg_room_site initializers
 *
 * Syntax sugar for _pcg_room_emit().
 */
#define pcg_room_emit(_ctx, _p, args...) _pcg_room_emit((_ctx), (_p), &(const pcg_room_site){ args })

#endif /* __CLAP_PCG_ROOM_H__ */
