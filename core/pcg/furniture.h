/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_FURNITURE_H__
#define __CLAP_PCG_FURNITURE_H__

#include "error.h"
#include "pcg/sink.h"
#include "pcg/surface.h"

/*
 * Office furniture: a desk, a chair, a monitor and a corkboard. Like any
 * other element, each piece is placed by its site's frame, which bakes
 * its yaw into the vertices, so pieces in one chunk that wear the same
 * material share one mesh however they are turned. None of them
 * collides by default: a room full of them costs a few draws, not one
 * per piece, and leaves the navmesh alone.
 */

/**
 * enum pcg_desk_surf - pcg_desk_params surfaces
 * @PCG_DESK_SHELL:     the whole desk
 * @PCG_DESK_NR_SURF:   number of surfaces
 */
enum pcg_desk_surf {
    PCG_DESK_SHELL,
    PCG_DESK_NR_SURF,
};

/**
 * struct pcg_desk_params - a desk's style
 * @surf:       per-surface finish, also by name
 * @w:          top width, along +a
 * @d:          top depth, along +c
 * @h:          height of the top's upper face
 * @top_t:      top thickness
 * @side_w:     thickness of each side panel, along +a
 * @side_inset: from each end of the top in to its side panel's outer face
 * @side_d:     depth of the side panels, centered under the top
 *
 * A slab top on two slab sides, each a box. The top's center is over the
 * site's floor point; the front, where the sitter is, faces +c, and the
 * back goes against a wall.
 */
typedef struct pcg_desk_params {
    union {
        pcg_surface surf[PCG_DESK_NR_SURF];
        struct {
            pcg_surface shell;
        };
    };
    float   w;
    float   d;
    float   h;
    float   top_t;
    float   side_w;
    float   side_inset;
    float   side_d;
} pcg_desk_params;

/**
 * pcg_desk_params_init() - an office desk's style
 * @p:  the params to fill, every field
 *
 * A 1.5 m by 0.7 m top 0.06 m thick at 0.72 m, on 0.12 m sides 0.62 m
 * deep, 0.02 m in from its ends. The shell is PCG_ROLE_FURNITURE and
 * PCG_COLLIDE_NONE.
 */
void pcg_desk_params_init(pcg_desk_params *p);

/**
 * struct pcg_desk_site - where a desk goes
 * @frame:  the floor point under the top's center: +c toward the sitter
 */
typedef struct pcg_desk_site {
    pcg_frame   frame;
} pcg_desk_site;

/**
 * pcg_desk_check() - check a desk's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a dimension that isn't finite, a
 * dimension other than @side_inset that isn't positive, a negative
 * @side_inset, a top as thick as the desk is high, sides deeper than the
 * top or sides that don't fit side by side under it
 */
cerr_check pcg_desk_check(const pcg_desk_params *p, const pcg_desk_site *site);

/**
 * _pcg_desk_emit() - emit a desk
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_desk_check() and pcg_box() errors
 */
cerr_check _pcg_desk_emit(const pcg_ctx *ctx, const pcg_desk_params *p, const pcg_desk_site *site);

/**
 * define pcg_desk_emit - emit a desk at a site described by pcg_desk_site initializers
 *
 * Syntax sugar for _pcg_desk_emit().
 */
#define pcg_desk_emit(_ctx, _p, args...) _pcg_desk_emit((_ctx), (_p), &(const pcg_desk_site){ args })

/**
 * enum pcg_chair_surf - pcg_chair_params surfaces
 * @PCG_CHAIR_SHELL:    the seat and the back
 * @PCG_CHAIR_BASE:     the pedestal: the stem and the disc it stands on
 * @PCG_CHAIR_NR_SURF:  number of surfaces
 */
enum pcg_chair_surf {
    PCG_CHAIR_SHELL,
    PCG_CHAIR_BASE,
    PCG_CHAIR_NR_SURF,
};

/**
 * struct pcg_chair_params - a pedestal chair's style
 * @seat_w:     seat width, along +a
 * @seat_d:     seat depth, along +c
 * @seat_h:     height of the seat's top
 * @seat_t:     seat thickness
 * @seat_c:     the seat's center along +c, off the pedestal's axis
 * @back_w:     back width
 * @back_h:     back height over the seat's top
 * @back_t:     back thickness; the back stands behind the seat's rear
 *              edge
 * @stem_r:     radius of the stem, from the disc up to the seat
 * @base_r:     radius of the disc the stem stands on
 * @base_h:     disc thickness
 * @segments:   sides of the stem and the disc, 3 to
 *              PCG_CYLINDER_SIDES_MAX
 * @surf:       per-surface finish, also by name
 *
 * A box seat and a box back on a stem standing on a disc. The pedestal's
 * axis is over the site's floor point, and the sitter faces +c.
 */
typedef struct pcg_chair_params {
    float   seat_w;
    float   seat_d;
    float   seat_h;
    float   seat_t;
    float   seat_c;
    float   back_w;
    float   back_h;
    float   back_t;
    float   stem_r;
    float   base_r;
    float   base_h;
    int     segments;
    union {
        pcg_surface surf[PCG_CHAIR_NR_SURF];
        struct {
            pcg_surface shell, base;
        };
    };
} pcg_chair_params;

/**
 * pcg_chair_params_init() - an office chair's style
 * @p:  the params to fill, every field
 *
 * A 0.44 m square seat 0.06 m thick at 0.48 m, 0.02 m forward of the
 * axis, a 0.42 m by 0.40 m back 0.06 m thick, on a stem of radius
 * 0.035 m standing on a disc of radius 0.24 m, 0.05 m thick, both
 * 10-sided. Both surfaces are PCG_ROLE_FURNITURE and PCG_COLLIDE_NONE.
 */
void pcg_chair_params_init(pcg_chair_params *p);

/**
 * struct pcg_chair_site - where a chair goes
 * @frame:  the floor point under the pedestal's axis: the sitter faces +c
 */
typedef struct pcg_chair_site {
    pcg_frame   frame;
} pcg_chair_site;

/**
 * pcg_chair_check() - check a chair's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a dimension that isn't finite, a
 * dimension other than @seat_c that isn't positive, fewer than 3
 * segments, or a seat too low for the disc and the seat's thickness;
 * CERR_TOO_LARGE for more than PCG_CYLINDER_SIDES_MAX segments
 */
cerr_check pcg_chair_check(const pcg_chair_params *p, const pcg_chair_site *site);

/**
 * _pcg_chair_emit() - emit a chair
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * The pedestal charts as pcg_cylinder() does in meters.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_chair_check(), pcg_box() and pcg_cylinder() errors
 */
cerr_check _pcg_chair_emit(const pcg_ctx *ctx, const pcg_chair_params *p, const pcg_chair_site *site);

/**
 * define pcg_chair_emit - emit a chair at a site described by pcg_chair_site initializers
 *
 * Syntax sugar for _pcg_chair_emit().
 */
#define pcg_chair_emit(_ctx, _p, args...) _pcg_chair_emit((_ctx), (_p), &(const pcg_chair_site){ args })

/**
 * enum pcg_monitor_surf - pcg_monitor_params surfaces
 * @PCG_MONITOR_SHELL:      the foot, the neck and the panel
 * @PCG_MONITOR_SCREEN:     the screen on the panel's front
 * @PCG_MONITOR_NR_SURF:    number of surfaces
 */
enum pcg_monitor_surf {
    PCG_MONITOR_SHELL,
    PCG_MONITOR_SCREEN,
    PCG_MONITOR_NR_SURF,
};

/**
 * struct pcg_monitor_params - a desktop monitor's style
 * @surf:       per-surface finish, also by name
 * @w:          panel width, along +a
 * @h:          panel height
 * @t:          panel thickness
 * @lift:       height of the panel's underside over the site's floor
 * @front:      where the panel's front face is along +c
 * @bezel:      the panel's border around the screen
 * @screen_off: how far the screen stands proud of the panel's front
 * @foot_w:     foot width
 * @foot_d:     foot depth
 * @foot_t:     foot thickness
 * @foot_c:     the foot's center along +c
 * @neck_w:     width of the neck from the foot up into the panel
 * @neck_d:     neck depth
 * @neck_h:     height of the neck's top
 * @neck_c:     the neck's center along +c
 * @shell_uv:   the texture rectangle every face of the foot, the neck and
 *              the panel maps
 * @screen_uv:  the texture rectangle the screen maps: (u0, v0) at its
 *              bottom corner on -a, (u1, v1) at its top corner on +a
 *
 * A panel on a neck on a foot, all boxes, with a screen quad standing
 * just proud of the panel's front. The monitor wears one texture atlas
 * (PCG_UV_UNIT): the shell maps one cell of it and the screen another,
 * so an atlas that only glows in its screen cell lights the screen
 * alone, and the whole monitor is one material. The screen faces +c;
 * the foot stands on the site's frame.
 */
typedef struct pcg_monitor_params {
    union {
        pcg_surface surf[PCG_MONITOR_NR_SURF];
        struct {
            pcg_surface shell, screen;
        };
    };
    float   w;
    float   h;
    float   t;
    float   lift;
    float   front;
    float   bezel;
    float   screen_off;
    float   foot_w;
    float   foot_d;
    float   foot_t;
    float   foot_c;
    float   neck_w;
    float   neck_d;
    float   neck_h;
    float   neck_c;
    vec4    shell_uv;
    vec4    screen_uv;
} pcg_monitor_params;

/**
 * pcg_monitor_params_init() - an all-in-one computer's style
 * @p:  the params to fill, every field
 *
 * A 0.54 m by 0.36 m panel 0.05 m thick, 0.10 m over the desk, with a
 * 0.025 m bezel and the screen 3 mm proud of it, on a 0.05 m by 0.04 m
 * neck up to 0.14 m and a 0.22 m by 0.16 m foot 0.02 m thick. The shell
 * maps [0.05, 0.40] of a PCG_PRESET_MONITOR atlas both ways, the screen
 * its right half. Both surfaces are PCG_ROLE_SCREEN and
 * PCG_COLLIDE_NONE.
 */
void pcg_monitor_params_init(pcg_monitor_params *p);

/**
 * struct pcg_monitor_site - where a monitor goes
 * @frame:  the point on the desk top under the foot: the screen faces +c
 */
typedef struct pcg_monitor_site {
    pcg_frame   frame;
} pcg_monitor_site;

/**
 * pcg_monitor_check() - check a monitor's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a dimension or a texture
 * coordinate that isn't finite, a size or @screen_off that isn't
 * positive, a negative @lift or @bezel, a bezel that leaves no screen,
 * or a neck that doesn't rise above the foot
 */
cerr_check pcg_monitor_check(const pcg_monitor_params *p, const pcg_monitor_site *site);

/**
 * _pcg_monitor_emit() - emit a monitor
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_monitor_check(), pcg_box() and pcg_quad() errors
 */
cerr_check _pcg_monitor_emit(const pcg_ctx *ctx, const pcg_monitor_params *p, const pcg_monitor_site *site);

/**
 * define pcg_monitor_emit - emit a monitor at a site described by pcg_monitor_site initializers
 *
 * Syntax sugar for _pcg_monitor_emit().
 */
#define pcg_monitor_emit(_ctx, _p, args...) _pcg_monitor_emit((_ctx), (_p), &(const pcg_monitor_site){ args })

/**
 * enum pcg_corkboard_surf - pcg_corkboard_params surfaces
 * @PCG_CORKBOARD_BOARD:    the whole board
 * @PCG_CORKBOARD_NR_SURF:  number of surfaces
 */
enum pcg_corkboard_surf {
    PCG_CORKBOARD_BOARD,
    PCG_CORKBOARD_NR_SURF,
};

/**
 * struct pcg_corkboard_params - a pinboard's style
 * @surf:       per-surface finish, also by name
 * @w:          board width, along +a
 * @h:          board height
 * @t:          board thickness
 * @center_y:   height of the board's center over the floor, at least
 *              half of @h
 * @standoff:   from the wall's face out to the board's back
 * @face_uv:    the texture rectangle the front maps: (u0, v0) at its
 *              bottom corner on -a, (u1, v1) at its top corner on +a
 * @rim_uv:     the texture rectangle the back and the edges map
 *
 * A thin board hanging on a wall, its front facing +c. It wears a
 * PCG_UV_UNIT texture: the front carries @face_uv, typically all of it,
 * and the other faces sit in a corner of its border.
 */
typedef struct pcg_corkboard_params {
    union {
        pcg_surface surf[PCG_CORKBOARD_NR_SURF];
        struct {
            pcg_surface board;
        };
    };
    float   w;
    float   h;
    float   t;
    float   center_y;
    float   standoff;
    vec4    face_uv;
    vec4    rim_uv;
} pcg_corkboard_params;

/**
 * pcg_corkboard_params_init() - an office pinboard's style
 * @p:  the params to fill, every field
 *
 * A 0.96 m by 0.68 m board 0.035 m thick, centered 1.55 m over the floor,
 * 5 mm off the wall. The front maps the whole texture upright, the rest
 * a sliver of its left border. The board is PCG_ROLE_BOARD and
 * PCG_COLLIDE_NONE.
 */
void pcg_corkboard_params_init(pcg_corkboard_params *p);

/**
 * struct pcg_corkboard_site - where a corkboard goes
 * @frame:  the line of the wall's face at floor level, under the board's
 *          center: +c out of the wall
 */
typedef struct pcg_corkboard_site {
    pcg_frame   frame;
} pcg_corkboard_site;

/**
 * pcg_corkboard_check() - check a corkboard's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a dimension or a texture
 * coordinate that isn't finite, a size that isn't positive, a negative
 * @standoff, or a board hanging into the floor
 */
cerr_check pcg_corkboard_check(const pcg_corkboard_params *p, const pcg_corkboard_site *site);

/**
 * _pcg_corkboard_emit() - emit a corkboard
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_corkboard_check() and pcg_quad() errors
 */
cerr_check _pcg_corkboard_emit(const pcg_ctx *ctx, const pcg_corkboard_params *p,
                               const pcg_corkboard_site *site);

/**
 * define pcg_corkboard_emit - emit a corkboard at a site described by pcg_corkboard_site initializers
 *
 * Syntax sugar for _pcg_corkboard_emit().
 */
#define pcg_corkboard_emit(_ctx, _p, args...) \
    _pcg_corkboard_emit((_ctx), (_p), &(const pcg_corkboard_site){ args })

#endif /* __CLAP_PCG_FURNITURE_H__ */
