// SPDX-License-Identifier: Apache-2.0
/*
 * Emitting glyphs and strings of glyphs into meshes: a glyph's outline is
 * triangulated into the front face in the XY plane, facing +Z, optionally
 * extruded along -Z (the side faces) and closed with a mirrored back face.
 */
#include "font.h"
#include "memory.h"
#include "mesh.h"
#include "primitives.h"
#include "text-mesh.h"
#include "triangulate.h"

static cerr text_mesh_emit_outline(const glyph_outline *ol, const text_mesh_opts *opts)
{
    if (!ol->nr_contours)   return CERR_OK;

    float size = opts->size ? opts->size : 1.0f;

    vec2 *points = mem_alloc(sizeof(*points), .nr = ol->nr_points);
    if (!points)    return CERR_NOMEM;

    for (size_t i = 0; i < ol->nr_points; i++) {
        points[i][0] = opts->org[0] + ol->points[i][0] * size;
        points[i][1] = opts->org[1] + ol->points[i][1] * size;
    }

    prim_extrusion ext = {
        .points     = points,
        .rings      = ol->contours,
        .nr_rings   = ol->nr_contours,
        .z          = opts->org[2],
        .depth      = opts->depth,
        .closed     = opts->closed,
    };

    cerr err = _prim_emit_extrusion(&ext, &(const prim_emit_opts){ .mesh = opts->mesh });
    mem_free(points);

    return err;
}

/**
 * typedef text_mesh_glyph_fn - text_mesh_for_each() callback
 * @ol:     the glyph's outline
 * @pen:    the pen position, em units, from the start of the string
 * @data:   the caller's data
 *
 * Return: CERR_OK to go on, or an error to stop the walk with
 */
typedef cerr (*text_mesh_glyph_fn)(const glyph_outline *ol, const vec2 pen, void *data);

/**
 * text_mesh_for_each() - walk a string's glyph outlines
 * @font:           font to take the glyphs from
 * @str:            the string
 * @nr_segments:    points per outline curve; 0 picks adaptively
 * @fn:             called for every glyph
 * @data:           @fn's data
 *
 * Advance the pen (in em units, starting at 0 on the baseline) by each
 * glyph's advance and pairwise kerning; '\n' returns the pen and moves
 * it a line down.
 * Return: CERR_OK, or the first error of font_glyph_outline() or @fn
 */
static cerr text_mesh_for_each(struct font *font, const char *str, unsigned int nr_segments,
                               text_mesh_glyph_fn fn, void *data)
{
    float line_height = font_line_height(font);
    if (line_height <= 0.0f)    line_height = 1.2f;

    vec2 pen = { 0.0f, 0.0f };
    unsigned char prev = 0;

    for (const char *p = str; *p; p++) {
        unsigned char c = *p;

        if (c == '\n') {
            pen[0] = 0.0f;
            pen[1] -= line_height;
            prev = 0;
            continue;
        }

        LOCAL_SET(glyph_outline, ol) = CRES_RET_CERR(font_glyph_outline(font, c, nr_segments));

        pen[0] += font_glyph_kerning(font, prev, c);
        CERR_RET_CERR(fn(ol, pen, data));
        pen[0] += ol->advance;
        prev = c;
    }

    return CERR_OK;
}

cerr _text_mesh_emit_glyph(struct font *font, unsigned char c, const text_mesh_opts *opts)
{
    if (!font || !opts || !opts->mesh)  return CERR_INVALID_ARGUMENTS;

    LOCAL_SET(glyph_outline, ol) = CRES_RET_CERR(font_glyph_outline(font, c, opts->nr_segments));

    return text_mesh_emit_outline(ol, opts);
}

static cerr emit_string_one(const glyph_outline *ol, const vec2 pen, void *data)
{
    const text_mesh_opts *opts = data;
    float size = opts->size ? opts->size : 1.0f;
    text_mesh_opts o = *opts;

    o.org[0] += pen[0] * size;
    o.org[1] += pen[1] * size;

    return text_mesh_emit_outline(ol, &o);
}

cerr _text_mesh_emit_string(struct font *font, const char *str, const text_mesh_opts *opts)
{
    if (!font || !str || !opts || !opts->mesh)  return CERR_INVALID_ARGUMENTS;

    return text_mesh_for_each(font, str, opts->nr_segments, emit_string_one, (void *)opts);
}
