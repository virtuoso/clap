// SPDX-License-Identifier: Apache-2.0
#include <float.h>
#include <math.h>
#include "memory.h"
#include "pcg/internal.h"
#include "pcg/room-row.h"

void pcg_room_row_params_init(pcg_room_row_params *p)
{
    *p = (pcg_room_row_params){};
    pcg_room_params_init(&p->room);
}

/*
 * struct row_room - one room of a row, picked and laid out
 * @entry:  its entry, as its fn picked it
 * @p:      its style, as its fn picked it
 * @site:   its site; the label points into @label
 * @label:  its sign text
 */
typedef struct row_room {
    pcg_room_entry  entry;
    pcg_room_params p;
    pcg_room_site   site;
    char            label[PCG_ROOM_LABEL_MAX];
} row_room;

static cerr row_site_check(const pcg_room_row_site *site)
{
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    if (!pcg_positive_finite(site->len))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room row length isn't positive and finite");
    if (!isfinite(site->depth_ref))     return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room row depth_ref isn't finite");
    if (site->nr_rooms && !site->rooms) return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room row rooms missing");

    return CERR_OK;
}

/* a room's width or depth, @frac of @ref or meters */
static cres(float) row_measure(float x, bool frac, float ref)
{
    float m = frac ? x * ref : x;

    if (!pcg_positive_finite(m))
        return cres_error(float, CERR_INVALID_ARGUMENTS, .fmt = "room row width or depth isn't positive and finite");

    return cres_val(float, m);
}

/* pick, lay out and label the rooms of the row at @site into @out */
static cerr row_layout(const pcg_room_row_params *p, const pcg_room_row_site *site, row_room *out)
{
    CERR_RET_CERR(row_site_check(site));

    float total = 0.0f;

    /* every room's entry and style, as its fn picks them */
    for (size_t k = 0; k < site->nr_rooms; k++) {
        pcg_room_entry *e = &out[k].entry;
        pcg_room_tag tag = site->tag;

        tag.number += (int)k;
        *e = site->rooms[k];
        out[k].p = p->room;
        if (e->fn)  e->fn(&tag, site->rooms[k].priv, e, &out[k].p);

        total += CRES_RET_CERR(row_measure(e->width, e->width_frac, site->len));
    }
    /*
     * Widths that fill the line add up past it by their rounding: each
     * fraction, product and sum rounds by up to half a float's epsilon
     */
    if (!(total <= site->len * (1.0f + (float)(site->nr_rooms + 1) * FLT_EPSILON)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room row rooms wider than the line");

    /* the strip, centered on the line */
    float at = (site->len - total) * 0.5f;
    for (size_t k = 0; k < site->nr_rooms; k++) {
        row_room *r = &out[k];
        const pcg_room_entry *e = &r->entry;
        float w = CRES_RET_CERR(row_measure(e->width, e->width_frac, site->len));
        float d = CRES_RET_CERR(row_measure(e->depth, e->depth_frac, site->depth_ref));
        uint32_t front = PCG_ROOM_SOLID(PCG_ROOM_FRONT) | PCG_ROOM_GLASS(PCG_ROOM_FRONT);

        r->site = (pcg_room_site){
            .frame      = site->frame,
            .a0         = at,
            .w          = w,
            .d          = d,
            .h          = site->h,
            .ceiling    = site->ceiling,
            .wall_mask  = e->wall_mask,
            .label      = r->label,
            .tag        = site->tag,
        };
        r->site.tag.number += (int)k;
        r->label[0] = '\0';
        if (p->label && e->wall_mask & front)   p->label(&r->site.tag, p->label_priv, r->label, sizeof(r->label));
        r->label[sizeof(r->label) - 1] = '\0';

        at += w;
    }

    return CERR_OK;
}

/* check all of @rooms */
static cerr rooms_check(const row_room *rooms, size_t nr)
{
    for (size_t k = 0; k < nr; k++)     CERR_RET_CERR(pcg_room_check(&rooms[k].p, &rooms[k].site));

    return CERR_OK;
}

/* check all of @rooms, then emit them */
static cerr rooms_emit(const pcg_ctx *ctx, const row_room *rooms, size_t nr)
{
    CERR_RET_CERR(rooms_check(rooms, nr));
    for (size_t k = 0; k < nr; k++)     CERR_RET_CERR(_pcg_room_emit(ctx, &rooms[k].p, &rooms[k].site));

    return CERR_OK;
}

cerr _pcg_room_row_emit(const pcg_ctx *ctx, const pcg_room_row_params *p, const pcg_room_row_site *site)
{
    if (!ctx || !p || !site)    return CERR_INVALID_ARGUMENTS;
    if (!site->nr_rooms)        return row_site_check(site);

    LOCAL_SET(void, buf) = mem_alloc(sizeof(row_room), .nr = site->nr_rooms);
    if (!buf)   return CERR_NOMEM;

    row_room *rooms = buf;
    CERR_RET_CERR(row_layout(p, site, rooms));

    return rooms_emit(ctx, rooms, site->nr_rooms);
}

static cerr ring_site_check(const pcg_room_ring_site *site)
{
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    CERR_RET_CERR(pcg_path_check(site->pts, site->nr_pts, true, "room ring polygon"));

    if (site->nr_skip && !site->skip)   return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room ring skip list missing");
    for (size_t i = 0; i < site->nr_skip; i++)
        if (site->skip[i] >= site->nr_pts)
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "room ring skips side %zu past the last one",
                .arg0   = (void *)(uintptr_t)site->skip[i]
            );

    return CERR_OK;
}

static bool ring_skips(const pcg_room_ring_site *site, size_t side)
{
    for (size_t i = 0; i < site->nr_skip; i++)
        if (site->skip[i] == side)  return true;

    return false;
}

/*
 * Pick, lay out and label the rooms of every row of the ring at @site
 * into @buf, allocated here; *@nr gets their number
 */
static cerr ring_layout(const pcg_room_row_params *p, const pcg_room_ring_site *site, void **buf, size_t *nr)
{
    CERR_RET_CERR(ring_site_check(site));

    /* a row on every side at most: the rows' offsets below stay under this count */
    size_t n = site->nr_pts, nr_rooms = site->nr_rooms, nr_all;
    if (mul_overflow(n, nr_rooms, &nr_all))
        return CERR_TOO_LARGE_REASON(.fmt = "room ring of more rooms than a size_t counts");

    *buf = mem_alloc(sizeof(row_room), .nr = nr_all ? nr_all : 1);
    if (!*buf)  return CERR_NOMEM;

    row_room *rooms = *buf;
    size_t nr_rows = 0;
    int number = site->tag.number;

    for (size_t i = 0; i < n; i++) {
        pcg_frame run;
        float len = CRES_RET_CERR(pcg_frame_run(&run, site->pts[i], site->pts[(i + 1) % n], 0.0f));

        if (ring_skips(site, i))    continue;

        /* the row's line in the caller's coordinates, as rooms place their children */
        pcg_room_row_site row = {
            .len        = len,
            .depth_ref  = site->depth_ref,
            .h          = site->h,
            .ceiling    = site->ceiling,
            .rooms      = site->rooms,
            .nr_rooms   = nr_rooms,
            .tag        = site->tag,
        };
        pcg_frame_compose(&site->frame, &run, &row.frame);
        row.tag.segment = (int)i;
        row.tag.number = number;

        CERR_RET_CERR(row_layout(p, &row, rooms + nr_rows * nr_rooms));
        nr_rows++;
        number += (int)nr_rooms;
    }

    *nr = nr_rows * nr_rooms;

    return CERR_OK;
}

cerr pcg_room_ring_check(const pcg_room_row_params *p, const pcg_room_ring_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;

    LOCAL(void, buf);
    size_t nr;
    CERR_RET_CERR(ring_layout(p, site, &buf, &nr));

    return rooms_check(buf, nr);
}

cerr _pcg_room_ring_emit(const pcg_ctx *ctx, const pcg_room_row_params *p, const pcg_room_ring_site *site)
{
    if (!ctx || !p || !site)    return CERR_INVALID_ARGUMENTS;

    LOCAL(void, buf);
    size_t nr;
    CERR_RET_CERR(ring_layout(p, site, &buf, &nr));

    return rooms_emit(ctx, buf, nr);
}
