/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_FIXTURE_H__
#define __CLAP_PCG_FIXTURE_H__

#include "error.h"
#include "pcg/sink.h"
#include "pcg/surface.h"

/**
 * enum pcg_fixture_surf - pcg_fixture_params surfaces
 * @PCG_FIXTURE_BODY:       the whole fitting
 * @PCG_FIXTURE_NR_SURF:    number of surfaces
 */
enum pcg_fixture_surf {
    PCG_FIXTURE_BODY,
    PCG_FIXTURE_NR_SURF,
};

/**
 * struct pcg_fixture_params - a ceiling light's style
 * @surf:       per-surface finish, also by name
 * @r:          disc radius, to its corners
 * @h:          disc thickness
 * @drop:       from the ceiling down to the disc's underside; at least
 *              @h, so the disc stays under the ceiling
 * @segments:   sides of the disc, 3 to PCG_CYLINDER_SIDES_MAX
 * @uv:         how the disc is charted (see pcg_cylinder_desc), the UV
 *              mode of the material @body wears
 *
 * A round fitting: an upright @segments-gon disc hanging under a
 * ceiling, centered over the site's floor point, its first corner on
 * the frame's +a.
 */
typedef struct pcg_fixture_params {
    union {
        pcg_surface surf[PCG_FIXTURE_NR_SURF];
        struct {
            pcg_surface body;
        };
    };
    float       r;
    float       h;
    float       drop;
    int         segments;
    pcg_uv_mode uv;
} pcg_fixture_params;

/**
 * pcg_fixture_params_init() - a generic ceiling light's style
 * @p:  the params to fill, every field
 *
 * A 12-sided disc 0.32 m across and 0.05 m thick, flush with the
 * ceiling: its drop is its thickness. Every face carries the whole
 * texture (PCG_UV_UNIT): a fitting that small reads as one glow. The
 * body is PCG_ROLE_FIXTURE and PCG_COLLIDE_NONE.
 */
void pcg_fixture_params_init(pcg_fixture_params *p);

/**
 * struct pcg_fixture_site - where a fixture goes
 * @frame:      the floor point under the fixture's center
 * @ceiling:    the height the fixture hangs from, over the frame's floor:
 *              the underside of what is above, such as a slab's soffit
 */
typedef struct pcg_fixture_site {
    pcg_frame   frame;
    float       ceiling;
} pcg_fixture_site;

/**
 * pcg_fixture_check() - check a ceiling light's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Everything _pcg_fixture_emit() checks before it emits anything. A
 * parent that places several children makes the checks for all of them
 * before any of them emits, so that a bad one leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a radius, thickness, drop or
 * ceiling that isn't finite, a radius or thickness that isn't positive,
 * fewer than 3 segments, a drop less than the thickness, a bad UV mode or
 * a ceiling too low to hang the disc above the floor; CERR_TOO_LARGE for
 * more than PCG_CYLINDER_SIDES_MAX segments
 */
cerr_check pcg_fixture_check(const pcg_fixture_params *p, const pcg_fixture_site *site);

/**
 * _pcg_fixture_emit() - emit a ceiling light
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_fixture_check() and pcg_cylinder() errors
 */
cerr_check _pcg_fixture_emit(const pcg_ctx *ctx, const pcg_fixture_params *p, const pcg_fixture_site *site);

/**
 * define pcg_fixture_emit - emit a ceiling light at a site described by pcg_fixture_site initializers
 *
 * Syntax sugar for _pcg_fixture_emit().
 */
#define pcg_fixture_emit(_ctx, _p, args...) _pcg_fixture_emit((_ctx), (_p), &(const pcg_fixture_site){ args })

#endif /* __CLAP_PCG_FIXTURE_H__ */
