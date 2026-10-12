// SPDX-License-Identifier: Apache-2.0
#include "pcg/facade.h"
#include "pcg/internal.h"

void pcg_facade_params_init(pcg_facade_params *p)
{
    *p = (pcg_facade_params){};
    pcg_window_wall_params_init(&p->wall);
}

static cerr facade_site_check(const pcg_facade_site *site)
{
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    CERR_RET_CERR(pcg_path_check(site->pts, site->nr_pts, true, "facade polygon"));

    if (site->nr_edges && !site->edges)     return CERR_INVALID_ARGUMENTS_REASON(.fmt = "facade edges missing");
    for (size_t i = 0; i < site->nr_edges; i++)
        if (site->edges[i].edge >= site->nr_pts || (i && site->edges[i].edge <= site->edges[i - 1].edge))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "facade edge entry %zu past the last side or out of order",
                .arg0   = (void *)(uintptr_t)i
            );

    return CERR_OK;
}

/*
 * Side @i of @site as a window wall's site; *@next is the first entry of
 * the site's edges not taken yet, and moves past the one for side @i
 */
static cerr facade_side(const pcg_facade_site *site, size_t i, size_t *next, pcg_window_wall_site *wall)
{
    pcg_frame run;
    float len = CRES_RET_CERR(pcg_frame_run(&run, site->pts[i], site->pts[(i + 1) % site->nr_pts], 0.0f));

    *wall = (pcg_window_wall_site){ .frame = run, .len = len, .h = site->h };
    if (*next < site->nr_edges && site->edges[*next].edge == i) {
        wall->openings = site->edges[*next].openings;
        wall->nr_openings = site->edges[*next].nr_openings;
        (*next)++;
    }

    return CERR_OK;
}

/* every side of @site in turn: emitted into @ctx with @emit, otherwise only checked */
static cerr facade_sides(const pcg_ctx *ctx, const pcg_facade_params *p, const pcg_facade_site *site, bool emit)
{
    size_t next = 0;

    for (size_t i = 0; i < site->nr_pts; i++) {
        pcg_window_wall_site wall;

        CERR_RET_CERR(facade_side(site, i, &next, &wall));
        if (emit)   CERR_RET_CERR(_pcg_window_wall_emit(ctx, &p->wall, &wall));
        else        CERR_RET_CERR(pcg_window_wall_check(&p->wall, &wall));
    }

    return CERR_OK;
}

cerr pcg_facade_check(const pcg_facade_params *p, const pcg_facade_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(facade_site_check(site));

    return facade_sides(nullptr, p, site, false);
}

cerr _pcg_facade_emit(const pcg_ctx *ctx, const pcg_facade_params *p, const pcg_facade_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_facade_check(p, site));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);

    return facade_sides(&at, p, site, true);
}
